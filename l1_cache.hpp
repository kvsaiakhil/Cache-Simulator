#ifndef L1_CACHE_HPP
#define L1_CACHE_HPP

#include "cache_block.hpp"
#include "cache_set.hpp"
#include "cache_config.hpp"
#include "cache_stats.hpp"
#include "replacement_policy.hpp"

#include <cstdint>
#include <list>
#include <ostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CacheBase {
public:
    virtual ~CacheBase() = default;

    virtual bool load(uint32_t byte_address, uint32_t& value) = 0;
    virtual bool store(uint32_t byte_address, uint32_t value) = 0;

    virtual uint64_t read_hits() const = 0;
    virtual uint64_t read_misses() const = 0;
    virtual uint64_t write_hits() const = 0;
    virtual uint64_t write_misses() const = 0;
    virtual uint64_t writebacks() const = 0;

    virtual void debug_print(std::ostream& os) const = 0;
};

template <uint32_t BlockSizeBytes>
class L1Cache : public CacheBase {
public:
    static constexpr uint32_t kWordSizeBytes = 4;
    using LineSnapshot = CacheLineSnapshot<BlockSizeBytes>;

    struct EvictionInfo {
        bool valid = false;
        uint32_t block_address = 0;
        LineSnapshot snapshot{};
    };

    static_assert(BlockSizeBytes > 0, "BlockSizeBytes must be non-zero");
    static_assert((BlockSizeBytes % kWordSizeBytes) == 0,
                  "BlockSizeBytes must be a multiple of 4");

    explicit L1Cache(const CacheConfig& config)
        : config_(config),
          cache_size_bytes_(config.cache_size_bytes),
          ways_(config.ways),
          num_sets_(0),
          offset_bits_(0),
          index_bits_(0),
          access_counter_(0),
          stats_() {
        if (cache_size_bytes_ == 0 || ways_ == 0) {
            throw std::invalid_argument("Cache geometry values must be non-zero");
        }
        if ((cache_size_bytes_ % BlockSizeBytes) != 0) {
            throw std::invalid_argument("cache_size_bytes must be a multiple of block size");
        }
        if (!is_power_of_two(BlockSizeBytes)) {
            throw std::invalid_argument("BlockSizeBytes must be a power of two");
        }

        const uint32_t total_blocks = cache_size_bytes_ / BlockSizeBytes;
        if (total_blocks < ways_) {
            throw std::invalid_argument("Cache has fewer total blocks than ways");
        }
        if ((total_blocks % ways_) != 0) {
            throw std::invalid_argument("total_blocks must be divisible by ways");
        }

        num_sets_ = total_blocks / ways_;
        if (!is_power_of_two(num_sets_)) {
            throw std::invalid_argument("Number of sets must be a power of two");
        }

        offset_bits_ = integer_log2(BlockSizeBytes);
        index_bits_ = integer_log2(num_sets_);

        for (uint32_t set_index = 0; set_index < num_sets_; ++set_index) {
            sets_.emplace_back(ways_, create_replacement_policy(config_.replacement_policy));
        }
    }

    bool load(uint32_t byte_address, uint32_t& value) override {
        if (access_load(byte_address, value)) {
            return true;
        }

        LineSnapshot zero_snapshot{};
        zero_snapshot.dirty = false;
        insert_or_update_line(byte_address, zero_snapshot);
        value = 0;
        return false;
    }

    bool store(uint32_t byte_address, uint32_t value) override {
        if (config_.write_policy != WritePolicy::WriteBack ||
            config_.write_miss_policy != WriteMissPolicy::WriteAllocate) {
            throw std::logic_error(
                "Standalone L1Cache::store supports only WriteBack+WriteAllocate; "
                "use CacheHierarchy for other write modes");
        }

        if (access_store(byte_address, value)) {
            return true;
        }

        if (config_.write_miss_policy == WriteMissPolicy::NoWriteAllocate) {
            return false;
        }

        LineSnapshot zero_snapshot{};
        zero_snapshot.dirty = (config_.write_policy == WritePolicy::WriteBack);
        write_word_to_snapshot(zero_snapshot, byte_offset_from_address(byte_address), value);
        insert_or_update_line(byte_address, zero_snapshot);
        return false;
    }

    bool access_load(uint32_t byte_address, uint32_t& value) {
        if ((byte_address % kWordSizeBytes) != 0) {
            throw std::invalid_argument("load address must be 4-byte aligned");
        }

        ++access_counter_;
        const uint32_t set_index = index_from_address(byte_address);
        const uint32_t tag = tag_from_address(byte_address);
        const uint32_t byte_offset = byte_offset_from_address(byte_address);

        auto& set = sets_[set_index];
        const int way = set.find_way_by_tag(tag);
        if (way >= 0) {
            CacheBlockBase& block = set.block_at(static_cast<uint32_t>(way));
            set.record_access(static_cast<uint32_t>(way), access_counter_);
            value = block.read_word(byte_offset);
            stats_.record_read_hit();
            update_shadow_cache(block_address(byte_address));
            return true;
        }

        stats_.record_read_miss();
        classify_miss(block_address(byte_address));
        update_shadow_cache(block_address(byte_address));
        return false;
    }

    bool access_store(uint32_t byte_address, uint32_t value) {
        if ((byte_address % kWordSizeBytes) != 0) {
            throw std::invalid_argument("store address must be 4-byte aligned");
        }

        ++access_counter_;
        const uint32_t set_index = index_from_address(byte_address);
        const uint32_t tag = tag_from_address(byte_address);
        const uint32_t byte_offset = byte_offset_from_address(byte_address);

        auto& set = sets_[set_index];
        const int way = set.find_way_by_tag(tag);
        if (way >= 0) {
            CacheBlockBase& block = set.block_at(static_cast<uint32_t>(way));
            block.write_word(byte_offset, value);
            block.set_dirty(config_.write_policy == WritePolicy::WriteBack);
            set.record_access(static_cast<uint32_t>(way), access_counter_);
            stats_.record_write_hit();
            update_shadow_cache(block_address(byte_address));
            return true;
        }

        stats_.record_write_miss();
        classify_miss(block_address(byte_address));
        update_shadow_cache(block_address(byte_address));
        return false;
    }

    uint64_t read_hits() const override {
        return stats_.read_hits();
    }

    uint64_t read_misses() const override {
        return stats_.read_misses();
    }

    uint64_t write_hits() const override {
        return stats_.write_hits();
    }

    uint64_t write_misses() const override {
        return stats_.write_misses();
    }

    uint64_t writebacks() const override {
        return stats_.writebacks();
    }

    uint64_t compulsory_misses() const {
        return stats_.compulsory_misses();
    }

    uint64_t capacity_misses() const {
        return stats_.capacity_misses();
    }

    uint64_t conflict_misses() const {
        return stats_.conflict_misses();
    }

    const CacheStats& stats() const {
        return stats_;
    }

    bool contains_line(uint32_t byte_address) const {
        const uint32_t set_index = index_from_address(byte_address);
        const uint32_t tag = tag_from_address(byte_address);
        return sets_[set_index].find_way_by_tag(tag) >= 0;
    }

    bool get_line_snapshot(uint32_t byte_address, LineSnapshot& snapshot) const {
        const uint32_t set_index = index_from_address(byte_address);
        const uint32_t tag = tag_from_address(byte_address);
        const auto& set = sets_[set_index];
        const int way = set.find_way_by_tag(tag);
        if (way < 0) {
            return false;
        }

        const CacheBlockBase& block = set.block_at(static_cast<uint32_t>(way));
        for (uint32_t i = 0; i < BlockSizeBytes; ++i) {
            snapshot.data[i] = block.read_byte(i);
        }
        snapshot.dirty = block.dirty();
        return true;
    }

    bool remove_line(uint32_t byte_address, LineSnapshot& snapshot) {
        const uint32_t set_index = index_from_address(byte_address);
        const uint32_t tag = tag_from_address(byte_address);
        auto& set = sets_[set_index];
        const int way = set.find_way_by_tag(tag);
        if (way < 0) {
            return false;
        }

        CacheBlockBase& block = set.block_at(static_cast<uint32_t>(way));
        for (uint32_t i = 0; i < BlockSizeBytes; ++i) {
            snapshot.data[i] = block.read_byte(i);
        }
        snapshot.dirty = block.dirty();
        block.reset();
        return true;
    }

    void insert_or_update_line(uint32_t byte_address,
                               const LineSnapshot& snapshot,
                               EvictionInfo* eviction_info = nullptr) {
        const uint32_t set_index = index_from_address(byte_address);
        const uint32_t tag = tag_from_address(byte_address);
        auto& set = sets_[set_index];

        ++access_counter_;
        const int existing_way = set.find_way_by_tag(tag);
        if (existing_way >= 0) {
            CacheBlockBase& block = set.block_at(static_cast<uint32_t>(existing_way));
            apply_snapshot(block, tag, snapshot);
            set.record_access(static_cast<uint32_t>(existing_way), access_counter_);
            return;
        }

        const uint32_t victim_way = set.choose_victim_way();
        CacheBlockBase& victim = set.block_at(victim_way);
        if (eviction_info != nullptr) {
            eviction_info->valid = victim.valid();
            if (victim.valid()) {
                eviction_info->block_address = compose_block_address(victim.tag(), set_index);
                eviction_info->snapshot = make_snapshot(victim);
            }
        }

        if (victim.valid() && victim.dirty() &&
            config_.write_policy == WritePolicy::WriteBack) {
            stats_.record_writeback();
        }

        CacheBlockBase& installed = set.install_block_at_way(victim_way, tag, access_counter_);
        apply_snapshot(installed, tag, snapshot);
    }

    uint32_t read_word_from_cache(uint32_t byte_address) const {
        const uint32_t set_index = index_from_address(byte_address);
        const uint32_t tag = tag_from_address(byte_address);
        const uint32_t byte_offset = byte_offset_from_address(byte_address);
        const auto& set = sets_[set_index];
        const int way = set.find_way_by_tag(tag);
        if (way < 0) {
            throw std::runtime_error("Requested block is not present in cache");
        }
        return set.block_at(static_cast<uint32_t>(way)).read_word(byte_offset);
    }

    void debug_print(std::ostream& os) const override {
        os << "L1 Cache State\n";
        for (uint32_t set_idx = 0; set_idx < num_sets_; ++set_idx) {
            os << " Set[" << set_idx << "]\n";
            const auto& set = sets_[set_idx];
            for (uint32_t way = 0; way < ways_; ++way) {
                const CacheBlockBase& block = set.block_at(way);
                os << "  Way[" << way << "]"
                   << " valid=" << block.valid()
                   << " dirty=" << block.dirty()
                   << " tag=0x" << std::hex << block.tag() << std::dec
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
    }

protected:
    static bool is_power_of_two(uint32_t value) {
        return value != 0 && (value & (value - 1)) == 0;
    }

    uint32_t index_from_address(uint32_t byte_address) const {
        return (byte_address >> offset_bits_) & (num_sets_ - 1);
    }

    uint32_t tag_from_address(uint32_t byte_address) const {
        return byte_address >> (offset_bits_ + index_bits_);
    }

    uint32_t byte_offset_from_address(uint32_t byte_address) const {
        return byte_address & (BlockSizeBytes - 1);
    }

    uint32_t block_address(uint32_t byte_address) const {
        return byte_address / BlockSizeBytes;
    }

    CacheBlockBase& install_block(uint32_t set_index, uint32_t tag) {
        auto& set = sets_[set_index];
        const uint32_t victim_way = set.choose_victim_way();
        CacheBlockBase& victim = set.block_at(victim_way);

        if (victim.valid() && victim.dirty() &&
            config_.write_policy == WritePolicy::WriteBack) {
            stats_.record_writeback();
        }

        return set.install_block_at_way(victim_way, tag, access_counter_);
    }

private:
    static uint32_t read_word_from_snapshot(const LineSnapshot& snapshot, uint32_t byte_offset) {
        return static_cast<uint32_t>(snapshot.data[byte_offset]) |
               (static_cast<uint32_t>(snapshot.data[byte_offset + 1]) << 8) |
               (static_cast<uint32_t>(snapshot.data[byte_offset + 2]) << 16) |
               (static_cast<uint32_t>(snapshot.data[byte_offset + 3]) << 24);
    }

    static void write_word_to_snapshot(LineSnapshot& snapshot,
                                       uint32_t byte_offset,
                                       uint32_t value) {
        snapshot.data[byte_offset] = static_cast<uint8_t>(value & 0xFFu);
        snapshot.data[byte_offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        snapshot.data[byte_offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        snapshot.data[byte_offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    }

    static LineSnapshot make_snapshot(const CacheBlockBase& block) {
        LineSnapshot snapshot{};
        for (uint32_t i = 0; i < BlockSizeBytes; ++i) {
            snapshot.data[i] = block.read_byte(i);
        }
        snapshot.dirty = block.dirty();
        return snapshot;
    }

    static void apply_snapshot(CacheBlockBase& block, uint32_t tag, const LineSnapshot& snapshot) {
        block.reset();
        block.set_tag(tag);
        block.set_valid(true);
        block.set_dirty(snapshot.dirty);
        for (uint32_t i = 0; i < BlockSizeBytes; ++i) {
            block.write_byte(i, snapshot.data[i]);
        }
    }

    uint32_t compose_block_address(uint32_t tag, uint32_t set_index) const {
        return (tag << index_bits_) | set_index;
    }

    void classify_miss(uint32_t block_addr) {
        if (seen_blocks_.find(block_addr) == seen_blocks_.end()) {
            stats_.record_compulsory_miss();
            seen_blocks_.insert(block_addr);
            return;
        }

        if (fa_block_positions_.find(block_addr) != fa_block_positions_.end()) {
            stats_.record_conflict_miss();
        } else {
            stats_.record_capacity_miss();
        }
    }

    void update_shadow_cache(uint32_t block_addr) {
        const auto existing = fa_block_positions_.find(block_addr);
        if (existing != fa_block_positions_.end()) {
            fa_lru_blocks_.erase(existing->second);
        } else if (fa_lru_blocks_.size() >= total_blocks()) {
            const uint32_t evicted_block = fa_lru_blocks_.back();
            fa_lru_blocks_.pop_back();
            fa_block_positions_.erase(evicted_block);
        }

        fa_lru_blocks_.push_front(block_addr);
        fa_block_positions_[block_addr] = fa_lru_blocks_.begin();
        seen_blocks_.insert(block_addr);
    }

    std::size_t total_blocks() const {
        return static_cast<std::size_t>(cache_size_bytes_ / BlockSizeBytes);
    }

    static uint32_t integer_log2(uint32_t value) {
        uint32_t bits = 0;
        while (value > 1) {
            value >>= 1;
            ++bits;
        }
        return bits;
    }

    CacheConfig config_;
    uint32_t cache_size_bytes_;
    uint32_t ways_;
    uint32_t num_sets_;

    uint32_t offset_bits_;
    uint32_t index_bits_;

    uint64_t access_counter_;
    CacheStats stats_;
    std::vector<CacheSet<BlockSizeBytes>> sets_;
    std::unordered_set<uint32_t> seen_blocks_;
    std::list<uint32_t> fa_lru_blocks_;
    std::unordered_map<uint32_t, typename std::list<uint32_t>::iterator> fa_block_positions_;
};

#endif
