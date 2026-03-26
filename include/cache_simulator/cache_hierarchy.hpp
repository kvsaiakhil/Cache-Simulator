#ifndef CACHE_HIERARCHY_HPP
#define CACHE_HIERARCHY_HPP

#include "cache_simulator/cache_config.hpp"
#include "cache_simulator/l1_cache.hpp"
#include "cache_simulator/victim_cache.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>

template <uint32_t BlockSizeBytes>
class BackingStore {
public:
    using LineSnapshot = CacheLineSnapshot<BlockSizeBytes>;

    LineSnapshot load_line(uint32_t block_address) const {
        const auto it = lines_.find(block_address);
        if (it != lines_.end()) {
            return it->second;
        }

        return LineSnapshot{};
    }

    void store_line(uint32_t block_address, LineSnapshot snapshot) {
        snapshot.dirty = false;
        lines_[block_address] = snapshot;
    }

private:
    mutable std::unordered_map<uint32_t, LineSnapshot> lines_;
};

template <uint32_t BlockSizeBytes>
using L2Cache = L1Cache<BlockSizeBytes>;

template <uint32_t BlockSizeBytes>
using L3Cache = L1Cache<BlockSizeBytes>;

template <uint32_t BlockSizeBytes>
class CacheHierarchy : public CacheBase {
public:
    using LineSnapshot = CacheLineSnapshot<BlockSizeBytes>;
    using EvictionInfo = typename L1Cache<BlockSizeBytes>::EvictionInfo;
    using VictimEvictionInfo = typename VictimCache<BlockSizeBytes>::EvictionInfo;

    explicit CacheHierarchy(const HierarchyConfig& config)
        : config_(config),
          l1_(config.l1),
          l2_(config.l2),
          l3_(config.l3),
          victim_cache_(config.victim_cache),
          memory_() {
        validate_policy_matrix();
    }

    bool load(uint32_t byte_address, uint32_t& value) override {
        if (l1_.access_load(byte_address, value)) {
            return true;
        }

        LineSnapshot snapshot{};
        if (victim_cache_.access_load(byte_address, value)) {
            take_from_victim_or_throw(byte_address, snapshot);
            insert_into_l1_cluster(byte_address, snapshot);
            value = l1_.read_word_from_cache(byte_address);
            return false;
        }

        if (l2_.access_load(byte_address, value)) {
            require_snapshot(l2_, byte_address, snapshot);
            if (config_.inclusion_policy == InclusionPolicy::Exclusive) {
                remove_or_throw(l2_, byte_address, snapshot);
            }
            fill_upper_levels(byte_address, snapshot, /*source_level=*/2);
            value = l1_.read_word_from_cache(byte_address);
            return false;
        }

        if (l3_.access_load(byte_address, value)) {
            require_snapshot(l3_, byte_address, snapshot);
            if (config_.inclusion_policy == InclusionPolicy::Exclusive) {
                remove_or_throw(l3_, byte_address, snapshot);
            }
            fill_upper_levels(byte_address, snapshot, /*source_level=*/3);
            value = l1_.read_word_from_cache(byte_address);
            return false;
        }

        snapshot = memory_.load_line(block_address(byte_address));
        fill_upper_levels(byte_address, snapshot, /*source_level=*/0);
        value = l1_.read_word_from_cache(byte_address);
        return false;
    }

    bool store(uint32_t byte_address, uint32_t value) override {
        if (is_write_through_mode()) {
            return store_write_through(byte_address, value);
        }

        return store_write_back(byte_address, value);
    }

    uint64_t read_hits() const override {
        return l1_.read_hits();
    }

    uint64_t read_misses() const override {
        return l1_.read_misses();
    }

    uint64_t write_hits() const override {
        return l1_.write_hits();
    }

    uint64_t write_misses() const override {
        return l1_.write_misses();
    }

    uint64_t writebacks() const override {
        return l1_.writebacks() + victim_cache_.writebacks() + l2_.writebacks() + l3_.writebacks();
    }

    const L1Cache<BlockSizeBytes>& l1() const { return l1_; }
    const VictimCache<BlockSizeBytes>& victim_cache() const { return victim_cache_; }
    const L2Cache<BlockSizeBytes>& l2() const { return l2_; }
    const L3Cache<BlockSizeBytes>& l3() const { return l3_; }

    std::string stats_csv() const {
        std::ostringstream os;
        os << CacheStats::csv_header() << "\n"
           << l1_.stats().to_csv_row("L1");
        if (victim_cache_.enabled()) {
            os << "\n" << victim_cache_.stats().to_csv_row("VC");
        }
        os << "\n"
           << l2_.stats().to_csv_row("L2") << "\n"
           << l3_.stats().to_csv_row("L3");
        return os.str();
    }

    std::string stats_json() const {
        std::ostringstream os;
        os << "{"
           << l1_.stats().to_json("L1");
        if (victim_cache_.enabled()) {
            os << "," << victim_cache_.stats().to_json("VC");
        }
        os << ","
           << l2_.stats().to_json("L2") << ","
           << l3_.stats().to_json("L3")
           << "}";
        return os.str();
    }

    void debug_print(std::ostream& os) const override {
        os << "=== L1 ===\n";
        l1_.debug_print(os);
        if (victim_cache_.enabled()) {
            os << "=== VC ===\n";
            victim_cache_.debug_print(os);
        }
        os << "=== L2 ===\n";
        l2_.debug_print(os);
        os << "=== L3 ===\n";
        l3_.debug_print(os);
    }

private:
    bool store_write_back(uint32_t byte_address, uint32_t value) {
        if (l1_.access_store(byte_address, value)) {
            return true;
        }

        LineSnapshot snapshot{};
        if (victim_cache_.access_store(byte_address, value, /*dirty=*/true)) {
            take_from_victim_or_throw(byte_address, snapshot);
            insert_into_l1_cluster(byte_address, snapshot);
            return false;
        }

        bool found = false;
        int source_level = 0;
        uint32_t ignored = 0;
        if (l2_.access_load(byte_address, ignored)) {
            require_snapshot(l2_, byte_address, snapshot);
            if (config_.inclusion_policy == InclusionPolicy::Exclusive) {
                remove_or_throw(l2_, byte_address, snapshot);
            }
            found = true;
            source_level = 2;
        } else if (l3_.access_load(byte_address, ignored)) {
            require_snapshot(l3_, byte_address, snapshot);
            if (config_.inclusion_policy == InclusionPolicy::Exclusive) {
                remove_or_throw(l3_, byte_address, snapshot);
            }
            found = true;
            source_level = 3;
        }

        if (!found) {
            snapshot = memory_.load_line(block_address(byte_address));
        }

        write_word_to_snapshot(snapshot, byte_offset(byte_address), value);
        snapshot.dirty = true;
        fill_upper_levels(byte_address, snapshot, source_level);
        return false;
    }

    bool store_write_through(uint32_t byte_address, uint32_t value) {
        if (l1_.access_store(byte_address, value)) {
            propagate_write_through_hit(byte_address, value);
            return true;
        }

        LineSnapshot snapshot{};
        if (victim_cache_.access_store(byte_address, value, /*dirty=*/false)) {
            take_from_victim_or_throw(byte_address, snapshot);
            insert_into_l1_cluster(byte_address, snapshot);
            propagate_write_through_hit(byte_address, value);
            return false;
        }

        bool lower_level_hit = false;
        if (l2_.access_store(byte_address, value)) {
            lower_level_hit = true;
            if (config_.inclusion_policy != InclusionPolicy::Exclusive &&
                l3_.contains_line(byte_address)) {
                const bool l3_hit = l3_.access_store(byte_address, value);
                if (!l3_hit) {
                    throw std::runtime_error("Inclusive/non-inclusive hierarchy lost lower-level line");
                }
            }
        } else if (l3_.access_store(byte_address, value)) {
            lower_level_hit = true;
        }

        write_through_to_memory(byte_address, value);
        (void)lower_level_hit;
        return false;
    }

    static uint32_t byte_offset(uint32_t byte_address) {
        return byte_address & (BlockSizeBytes - 1);
    }

    static uint32_t block_address(uint32_t byte_address) {
        return byte_address / BlockSizeBytes;
    }

    static void write_word_to_snapshot(LineSnapshot& snapshot,
                                       uint32_t byte_offset,
                                       uint32_t value) {
        snapshot.data[byte_offset] = static_cast<uint8_t>(value & 0xFFu);
        snapshot.data[byte_offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        snapshot.data[byte_offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        snapshot.data[byte_offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    }

    void write_through_to_memory(uint32_t byte_address, uint32_t value) {
        LineSnapshot snapshot = memory_.load_line(block_address(byte_address));
        write_word_to_snapshot(snapshot, byte_offset(byte_address), value);
        snapshot.dirty = false;
        memory_.store_line(block_address(byte_address), snapshot);
    }

    static void require_snapshot(const L1Cache<BlockSizeBytes>& cache,
                                 uint32_t byte_address,
                                 LineSnapshot& snapshot) {
        if (!cache.get_line_snapshot(byte_address, snapshot)) {
            throw std::runtime_error("Hierarchy expected resident cache line");
        }
    }

    static void remove_or_throw(L1Cache<BlockSizeBytes>& cache,
                                uint32_t byte_address,
                                LineSnapshot& snapshot) {
        if (!cache.remove_line(byte_address, snapshot)) {
            throw std::runtime_error("Hierarchy failed to remove resident cache line");
        }
    }

    void take_from_victim_or_throw(uint32_t byte_address, LineSnapshot& snapshot) {
        if (!victim_cache_.remove_line(byte_address, snapshot)) {
            throw std::runtime_error("Hierarchy expected resident victim cache line");
        }
    }

    void fill_upper_levels(uint32_t byte_address, const LineSnapshot& snapshot, int source_level) {
        if (config_.inclusion_policy == InclusionPolicy::Inclusive) {
            if (source_level == 0) {
                insert_into_level(l3_, byte_address, snapshot, nullptr, nullptr);
            }
            if (source_level == 0 || source_level == 3) {
                insert_into_level(l2_, byte_address, snapshot, &l3_, nullptr);
            }
            insert_into_l1_cluster(byte_address, snapshot);
            return;
        }

        if (config_.inclusion_policy == InclusionPolicy::NonInclusiveNonExclusive) {
            if (source_level == 0) {
                insert_into_level(l3_, byte_address, snapshot, nullptr, nullptr);
            }
            if (source_level == 0 || source_level == 3) {
                insert_into_level(l2_, byte_address, snapshot, &l3_, nullptr);
            }
            insert_into_l1_cluster(byte_address, snapshot);
            return;
        }

        // Exclusive
        insert_into_l1_cluster(byte_address, snapshot);
    }

    void insert_into_l1_cluster(uint32_t byte_address, const LineSnapshot& snapshot) {
        EvictionInfo l1_eviction{};
        l1_.insert_or_update_line(byte_address, snapshot, &l1_eviction);
        if (!l1_eviction.valid) {
            return;
        }

        if (victim_cache_.enabled()) {
            VictimEvictionInfo victim_eviction{};
            victim_cache_.insert_or_update_line(l1_eviction.block_address * BlockSizeBytes,
                                                l1_eviction.snapshot,
                                                &victim_eviction);
            if (victim_eviction.valid) {
                handle_top_cluster_eviction(victim_eviction.block_address,
                                            victim_eviction.snapshot);
            }
            return;
        }

        handle_top_cluster_eviction(l1_eviction.block_address, l1_eviction.snapshot);
    }

    void handle_top_cluster_eviction(uint32_t evicted_block_address,
                                     const LineSnapshot& snapshot) {
        if (config_.inclusion_policy == InclusionPolicy::Exclusive) {
            demote_top_cluster_line(evicted_block_address, snapshot);
            return;
        }

        if (snapshot.dirty) {
            insert_into_level(l2_, evicted_block_address * BlockSizeBytes, snapshot, &l3_, nullptr);
        }
    }

    void demote_top_cluster_line(uint32_t evicted_block_address,
                                 const LineSnapshot& snapshot) {
        EvictionInfo demoted{};
        l2_.insert_or_update_line(evicted_block_address * BlockSizeBytes, snapshot, &demoted);
        if (!demoted.valid) {
            return;
        }

        EvictionInfo demoted_to_final{};
        l3_.insert_or_update_line(demoted.block_address * BlockSizeBytes,
                                  demoted.snapshot,
                                  &demoted_to_final);
        if (demoted_to_final.valid && demoted_to_final.snapshot.dirty) {
            memory_.store_line(demoted_to_final.block_address, demoted_to_final.snapshot);
        }
    }

    void invalidate_upper_cluster_line(uint32_t byte_address) {
        LineSnapshot discarded{};
        l1_.remove_line(byte_address, discarded);
        if (victim_cache_.enabled()) {
            victim_cache_.remove_line(byte_address, discarded);
        }
    }

    void insert_into_level(L1Cache<BlockSizeBytes>& target,
                           uint32_t byte_address,
                           const LineSnapshot& snapshot,
                           L1Cache<BlockSizeBytes>* next_level,
                           L1Cache<BlockSizeBytes>* final_level) {
        EvictionInfo eviction{};
        target.insert_or_update_line(byte_address, snapshot, &eviction);
        if (!eviction.valid) {
            return;
        }

        if (config_.inclusion_policy == InclusionPolicy::Inclusive) {
            if (&target == &l3_) {
                l2_.remove_line(eviction.block_address * BlockSizeBytes, eviction.snapshot);
                invalidate_upper_cluster_line(eviction.block_address * BlockSizeBytes);
            } else if (&target == &l2_) {
                invalidate_upper_cluster_line(eviction.block_address * BlockSizeBytes);
            }
            if (eviction.snapshot.dirty) {
                propagate_dirty_eviction(target, eviction.block_address, eviction.snapshot);
            }
            return;
        }

        if (config_.inclusion_policy == InclusionPolicy::NonInclusiveNonExclusive) {
            if (eviction.snapshot.dirty) {
                propagate_dirty_eviction(target, eviction.block_address, eviction.snapshot);
            }
            return;
        }

        if (config_.inclusion_policy == InclusionPolicy::Exclusive) {
            if (next_level != nullptr) {
                EvictionInfo demoted{};
                next_level->insert_or_update_line(eviction.block_address * BlockSizeBytes,
                                                  eviction.snapshot,
                                                  &demoted);
                if (demoted.valid) {
                    if (final_level != nullptr) {
                        EvictionInfo demoted_to_final{};
                        final_level->insert_or_update_line(demoted.block_address * BlockSizeBytes,
                                                           demoted.snapshot,
                                                           &demoted_to_final);
                        if (demoted_to_final.valid && demoted_to_final.snapshot.dirty) {
                            memory_.store_line(demoted_to_final.block_address,
                                               demoted_to_final.snapshot);
                        }
                    } else if (demoted.snapshot.dirty) {
                        memory_.store_line(demoted.block_address, demoted.snapshot);
                    }
                }
            } else if (eviction.snapshot.dirty) {
                memory_.store_line(eviction.block_address, eviction.snapshot);
            }
        }
    }

    void propagate_dirty_eviction(L1Cache<BlockSizeBytes>& source,
                                  uint32_t evicted_block_address,
                                  const LineSnapshot& snapshot) {
        if (&source == &l1_) {
            insert_into_level(l2_, evicted_block_address * BlockSizeBytes, snapshot, &l3_, nullptr);
            return;
        }
        if (&source == &l2_) {
            insert_into_level(l3_, evicted_block_address * BlockSizeBytes, snapshot, nullptr, nullptr);
            return;
        }
        memory_.store_line(evicted_block_address, snapshot);
    }

    void propagate_write_through_hit(uint32_t byte_address, uint32_t value) {
        if (config_.inclusion_policy == InclusionPolicy::Inclusive &&
            !l2_.contains_line(byte_address)) {
            throw std::runtime_error("Inclusive hierarchy expected resident line in L2");
        }
        if (config_.inclusion_policy == InclusionPolicy::Inclusive &&
            !l3_.contains_line(byte_address)) {
            throw std::runtime_error("Inclusive hierarchy expected resident line in L3");
        }

        if (l2_.contains_line(byte_address)) {
            const bool l2_hit = l2_.access_store(byte_address, value);
            if (!l2_hit) {
                throw std::runtime_error("Hierarchy expected resident line in L2");
            }
        }

        if (l3_.contains_line(byte_address)) {
            const bool l3_hit = l3_.access_store(byte_address, value);
            if (!l3_hit) {
                throw std::runtime_error("Hierarchy expected resident line in L3");
            }
        }

        write_through_to_memory(byte_address, value);
    }

    bool is_supported_pair(const CacheConfig& cache_config) const {
        const bool write_back_pair =
            cache_config.write_policy == WritePolicy::WriteBack &&
            cache_config.write_miss_policy == WriteMissPolicy::WriteAllocate;
        const bool write_through_pair =
            cache_config.write_policy == WritePolicy::WriteThrough &&
            cache_config.write_miss_policy == WriteMissPolicy::NoWriteAllocate;
        return write_back_pair || write_through_pair;
    }

    bool same_write_mode_across_levels() const {
        return config_.l1.write_policy == config_.l2.write_policy &&
               config_.l2.write_policy == config_.l3.write_policy &&
               config_.l1.write_miss_policy == config_.l2.write_miss_policy &&
               config_.l2.write_miss_policy == config_.l3.write_miss_policy;
    }

    bool is_write_through_mode() const {
        return config_.l1.write_policy == WritePolicy::WriteThrough;
    }

    void validate_policy_matrix() const {
        if (!same_write_mode_across_levels()) {
            throw std::invalid_argument(
                "Hierarchy requires the same write policy and write-miss policy on L1/L2/L3");
        }

        if (!is_supported_pair(config_.l1) ||
            !is_supported_pair(config_.l2) ||
            !is_supported_pair(config_.l3)) {
            throw std::invalid_argument(
                "Supported hierarchy policy pairs are only WriteBack+WriteAllocate or "
                "WriteThrough+NoWriteAllocate");
        }
    }

    HierarchyConfig config_;
    L1Cache<BlockSizeBytes> l1_;
    L2Cache<BlockSizeBytes> l2_;
    L3Cache<BlockSizeBytes> l3_;
    VictimCache<BlockSizeBytes> victim_cache_;
    BackingStore<BlockSizeBytes> memory_;
};

#endif
