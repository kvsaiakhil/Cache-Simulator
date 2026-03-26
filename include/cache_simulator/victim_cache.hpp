#ifndef VICTIM_CACHE_HPP
#define VICTIM_CACHE_HPP

#include "cache_simulator/cache_config.hpp"
#include "cache_simulator/cache_set.hpp"
#include "cache_simulator/cache_stats.hpp"

#include <cstdint>
#include <memory>
#include <ostream>
#include <stdexcept>

template <uint32_t BlockSizeBytes>
class VictimCache {
public:
    static constexpr uint32_t kWordSizeBytes = 4;
    using LineSnapshot = CacheLineSnapshot<BlockSizeBytes>;

    struct EvictionInfo {
        bool valid = false;
        uint32_t block_address = 0;
        LineSnapshot snapshot{};
    };

    explicit VictimCache(const VictimCacheConfig& config)
        : enabled_(config.enabled),
          entries_(config.entries),
          access_counter_(0),
          stats_(),
          set_() {
        static_assert(BlockSizeBytes > 0, "BlockSizeBytes must be non-zero");
        static_assert((BlockSizeBytes % kWordSizeBytes) == 0,
                      "BlockSizeBytes must be a multiple of 4");

        if (!enabled_) {
            return;
        }
        if (entries_ == 0) {
            throw std::invalid_argument("Victim cache entries must be non-zero when enabled");
        }

        set_ = std::make_unique<CacheSet<BlockSizeBytes>>(
            entries_, create_replacement_policy(config.replacement_policy));
    }

    bool enabled() const { return enabled_; }
    uint32_t entries() const { return entries_; }

    bool contains_line(uint32_t byte_address) const {
        if (!enabled_) {
            return false;
        }
        return set_->find_way_by_tag(block_address(byte_address)) >= 0;
    }

    bool access_load(uint32_t byte_address, uint32_t& value) {
        if (!enabled_) {
            return false;
        }
        validate_aligned_address(byte_address, "load");

        ++access_counter_;
        const uint32_t tag = block_address(byte_address);
        const uint32_t byte_offset = byte_offset_from_address(byte_address);
        const int way = set_->find_way_by_tag(tag);
        if (way < 0) {
            stats_.record_read_miss();
            return false;
        }

        CacheBlockBase& block = set_->block_at(static_cast<uint32_t>(way));
        set_->record_access(static_cast<uint32_t>(way), access_counter_);
        value = block.read_word(byte_offset);
        stats_.record_read_hit();
        return true;
    }

    bool access_store(uint32_t byte_address, uint32_t value, bool dirty) {
        if (!enabled_) {
            return false;
        }
        validate_aligned_address(byte_address, "store");

        ++access_counter_;
        const uint32_t tag = block_address(byte_address);
        const uint32_t byte_offset = byte_offset_from_address(byte_address);
        const int way = set_->find_way_by_tag(tag);
        if (way < 0) {
            stats_.record_write_miss();
            return false;
        }

        CacheBlockBase& block = set_->block_at(static_cast<uint32_t>(way));
        block.write_word(byte_offset, value);
        block.set_dirty(dirty);
        set_->record_access(static_cast<uint32_t>(way), access_counter_);
        stats_.record_write_hit();
        return true;
    }

    bool get_line_snapshot(uint32_t byte_address, LineSnapshot& snapshot) const {
        if (!enabled_) {
            return false;
        }
        const uint32_t tag = block_address(byte_address);
        const int way = set_->find_way_by_tag(tag);
        if (way < 0) {
            return false;
        }
        const CacheBlockBase& block = set_->block_at(static_cast<uint32_t>(way));
        snapshot = make_snapshot(block);
        return true;
    }

    bool remove_line(uint32_t byte_address, LineSnapshot& snapshot) {
        if (!enabled_) {
            return false;
        }
        const uint32_t tag = block_address(byte_address);
        const int way = set_->find_way_by_tag(tag);
        if (way < 0) {
            return false;
        }
        CacheBlockBase& block = set_->block_at(static_cast<uint32_t>(way));
        snapshot = make_snapshot(block);
        block.reset();
        return true;
    }

    void insert_or_update_line(uint32_t byte_address,
                               const LineSnapshot& snapshot,
                               EvictionInfo* eviction_info = nullptr) {
        if (!enabled_) {
            return;
        }
        const uint32_t tag = block_address(byte_address);

        ++access_counter_;
        const int existing_way = set_->find_way_by_tag(tag);
        if (existing_way >= 0) {
            CacheBlockBase& block = set_->block_at(static_cast<uint32_t>(existing_way));
            apply_snapshot(block, tag, snapshot);
            set_->record_access(static_cast<uint32_t>(existing_way), access_counter_);
            return;
        }

        const uint32_t victim_way = set_->choose_victim_way();
        CacheBlockBase& victim = set_->block_at(victim_way);
        if (eviction_info != nullptr) {
            eviction_info->valid = victim.valid();
            if (victim.valid()) {
                eviction_info->block_address = victim.tag();
                eviction_info->snapshot = make_snapshot(victim);
            }
        }

        if (victim.valid() && victim.dirty()) {
            stats_.record_writeback();
        }

        CacheBlockBase& installed = set_->install_block_at_way(victim_way, tag, access_counter_);
        apply_snapshot(installed, tag, snapshot);
    }

    uint64_t read_hits() const { return stats_.read_hits(); }
    uint64_t read_misses() const { return stats_.read_misses(); }
    uint64_t write_hits() const { return stats_.write_hits(); }
    uint64_t write_misses() const { return stats_.write_misses(); }
    uint64_t writebacks() const { return stats_.writebacks(); }

    const CacheStats& stats() const { return stats_; }

    void debug_print(std::ostream& os) const {
        os << "Victim Cache State\n";
        if (!enabled_) {
            os << " disabled\n";
            return;
        }

        os << " entries=" << entries_ << "\n";
        for (uint32_t way = 0; way < entries_; ++way) {
            const CacheBlockBase& block = set_->block_at(way);
            os << "  Entry[" << way << "]"
               << " valid=" << block.valid()
               << " dirty=" << block.dirty()
               << " block=0x" << std::hex << block.tag() << std::dec
               << " bytes=[";
            for (uint32_t i = 0; i < block.block_size_bytes(); ++i) {
                if (i != 0) {
                    os << ",";
                }
                os << static_cast<uint32_t>(block.read_byte(i));
            }
            os << "]\n";
        }
    }

private:
    static uint32_t block_address(uint32_t byte_address) {
        return byte_address / BlockSizeBytes;
    }

    static uint32_t byte_offset_from_address(uint32_t byte_address) {
        return byte_address & (BlockSizeBytes - 1);
    }

    static void validate_aligned_address(uint32_t byte_address, const char* operation) {
        if ((byte_address % kWordSizeBytes) != 0) {
            throw std::invalid_argument(std::string(operation) +
                                        " address must be 4-byte aligned");
        }
    }

    static LineSnapshot make_snapshot(const CacheBlockBase& block) {
        LineSnapshot snapshot{};
        for (uint32_t i = 0; i < BlockSizeBytes; ++i) {
            snapshot.data[i] = block.read_byte(i);
        }
        snapshot.dirty = block.dirty();
        return snapshot;
    }

    static void apply_snapshot(CacheBlockBase& block,
                               uint32_t tag,
                               const LineSnapshot& snapshot) {
        block.reset();
        block.set_tag(tag);
        block.set_valid(true);
        block.set_dirty(snapshot.dirty);
        for (uint32_t i = 0; i < BlockSizeBytes; ++i) {
            block.write_byte(i, snapshot.data[i]);
        }
    }

    bool enabled_;
    uint32_t entries_;
    uint64_t access_counter_;
    CacheStats stats_;
    std::unique_ptr<CacheSet<BlockSizeBytes>> set_;
};

#endif
