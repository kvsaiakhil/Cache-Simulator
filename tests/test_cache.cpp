#include "cache_simulator/cache_hierarchy.hpp"
#include "cache_simulator/cache_set.hpp"
#include "cache_simulator/cache_stats.hpp"
#include "cache_simulator/l1_cache.hpp"
#include "cache_simulator/replacement_policy.hpp"
#include "cache_simulator/trace_runner.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_equal(uint32_t actual, uint32_t expected, const std::string& message) {
    if (actual != expected) {
        std::ostringstream os;
        os << message << " expected=" << expected << " actual=" << actual;
        throw std::runtime_error(os.str());
    }
}

void expect_equal_u64(uint64_t actual, uint64_t expected, const std::string& message) {
    if (actual != expected) {
        std::ostringstream os;
        os << message << " expected=" << expected << " actual=" << actual;
        throw std::runtime_error(os.str());
    }
}

void expect_contains(const std::string& haystack,
                     const std::string& needle,
                     const std::string& message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message + " missing=" + needle);
    }
}

uint32_t read_word_from_snapshot_bytes(const CacheLineSnapshot<16>& snapshot, uint32_t byte_offset) {
    return static_cast<uint32_t>(snapshot.data[byte_offset]) |
           (static_cast<uint32_t>(snapshot.data[byte_offset + 1]) << 8) |
           (static_cast<uint32_t>(snapshot.data[byte_offset + 2]) << 16) |
           (static_cast<uint32_t>(snapshot.data[byte_offset + 3]) << 24);
}

template <uint32_t BlockSizeBytes>
void expect_trace_case(const std::filesystem::path& trace_path,
                       uint64_t expected_operations,
                       uint64_t expected_read_hits,
                       uint64_t expected_read_misses,
                       uint64_t expected_compulsory,
                       uint64_t expected_capacity,
                       uint64_t expected_conflict,
                       const std::string& message_prefix) {
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    L1Cache<BlockSizeBytes> cache(config);
    TraceRunner<L1Cache<BlockSizeBytes>> runner(cache);
    std::ostringstream output;
    const TraceResult result = runner.run_file(trace_path.string(), output);

    expect_equal_u64(result.operations, expected_operations, message_prefix + ": operations");
    expect_equal_u64(cache.read_hits(), expected_read_hits, message_prefix + ": read hits");
    expect_equal_u64(cache.read_misses(), expected_read_misses, message_prefix + ": read misses");
    expect_equal_u64(cache.compulsory_misses(), expected_compulsory,
                     message_prefix + ": compulsory misses");
    expect_equal_u64(cache.capacity_misses(), expected_capacity,
                     message_prefix + ": capacity misses");
    expect_equal_u64(cache.conflict_misses(), expected_conflict,
                     message_prefix + ": conflict misses");
}

void test_lru_policy_prefers_least_recently_used() {
    LruReplacementPolicy policy;
    policy.reset(2);
    policy.on_insert(0, 1);
    policy.on_insert(1, 2);
    policy.on_access(0, 3);

    const uint32_t victim = policy.choose_victim({true, true});
    expect_equal(victim, 1, "LRU should evict the least recently used way");
}

void test_create_replacement_policy_returns_instances_for_all_supported_types() {
    expect_true(static_cast<bool>(create_replacement_policy(ReplacementPolicyType::LRU)),
                "Factory should create an LRU replacement policy");
    expect_true(static_cast<bool>(create_replacement_policy(ReplacementPolicyType::FIFO)),
                "Factory should create a FIFO replacement policy");
    expect_true(static_cast<bool>(create_replacement_policy(ReplacementPolicyType::Random)),
                "Factory should create a Random replacement policy");
}

void test_fifo_policy_prefers_oldest_insertion() {
    FifoReplacementPolicy policy;
    policy.reset(2);
    policy.on_insert(0, 1);
    policy.on_insert(1, 2);
    policy.on_access(0, 3);

    const uint32_t victim = policy.choose_victim({true, true});
    expect_equal(victim, 0, "FIFO should ignore access recency");
}

void test_random_policy_is_deterministic_and_uses_empty_way_first() {
    RandomReplacementPolicy policy;
    policy.reset(2);

    const uint32_t empty_victim = policy.choose_victim({true, false});
    expect_equal(empty_victim, 1, "Random policy should prefer an invalid way before random eviction");

    const uint32_t first_random = policy.choose_victim({true, true});
    const uint32_t second_random = policy.choose_victim({true, true});
    expect_equal(first_random, 1, "Random policy first deterministic victim mismatch");
    expect_equal(second_random, 0, "Random policy second deterministic victim mismatch");
}

void test_l1_cache_fifo_eviction_behavior() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        32,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::FIFO};

    L1Cache<kBlockSizeBytes> cache(config);
    expect_true(!cache.store(0x00, 0xAAAA5555u), "Initial FIFO store should miss");
    expect_true(!cache.store(0x10, 0xBBBB6666u), "Second FIFO store should miss");
    uint32_t value = 0;
    expect_true(cache.load(0x00, value), "Load should hit before eviction");

    expect_true(!cache.store(0x20, 0xCCCC7777u), "Third FIFO store should miss and evict oldest line");

    expect_true(cache.load(0x10, value), "FIFO should keep the second inserted line");
    expect_true(!cache.load(0x00, value), "FIFO should evict the oldest inserted line");
}

void test_l1_cache_random_policy_wires_through_config() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        32,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::Random};

    L1Cache<kBlockSizeBytes> cache(config);
    expect_true(!cache.store(0x00, 0x11111111u), "Initial Random store should miss");
    expect_true(!cache.store(0x10, 0x22222222u), "Second Random store should miss");
    expect_true(!cache.store(0x20, 0x33333333u), "Third Random store should miss and randomly evict one way");

    uint32_t value = 0;
    expect_true(cache.load(0x00, value), "Deterministic Random policy should keep first line with default seed");
    expect_true(cache.load(0x20, value), "Inserted line should be present after allocation");
    expect_true(!cache.load(0x10, value), "Deterministic Random policy should evict second line with default seed");
}

void test_l1_cache_lru_eviction_behavior() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        32,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    L1Cache<kBlockSizeBytes> cache(config);
    expect_true(!cache.store(0x00, 0xAAAA0001u), "Initial LRU store should miss");
    expect_true(!cache.store(0x10, 0xBBBB0002u), "Second LRU store should miss");

    uint32_t value = 0;
    expect_true(cache.load(0x00, value), "Load should hit and make first line most recent");
    expect_true(!cache.store(0x20, 0xCCCC0003u), "Third LRU store should miss and evict least recent line");

    expect_true(cache.load(0x00, value), "LRU should retain recently used first line");
    expect_true(!cache.load(0x10, value), "LRU should evict least recently used second line");
}

void test_l1_cache_counts_writebacks_on_dirty_eviction() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        32,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::FIFO};

    L1Cache<kBlockSizeBytes> cache(config);
    cache.store(0x00, 1);
    cache.store(0x10, 2);
    cache.store(0x20, 3);

    expect_equal_u64(cache.writebacks(), 1, "Dirty eviction should increment writeback count");
}

void test_cache_block_word_and_byte_access_round_trip() {
    CacheBlock<16> block;
    block.set_tag(0xAB);
    block.set_valid(true);
    block.set_dirty(true);
    block.write_word(4, 0x12345678u);

    expect_equal(block.tag(), 0xAB, "CacheBlock should retain tag");
    expect_true(block.valid(), "CacheBlock should retain valid bit");
    expect_true(block.dirty(), "CacheBlock should retain dirty bit");
    expect_equal(block.read_word(4), 0x12345678u, "CacheBlock should round-trip word writes");
    expect_equal(block.read_byte(4), 0x78u, "CacheBlock should store words in little-endian order");
    expect_equal(block.read_byte(5), 0x56u, "CacheBlock should store second byte correctly");
    expect_equal(block.read_byte(6), 0x34u, "CacheBlock should store third byte correctly");
    expect_equal(block.read_byte(7), 0x12u, "CacheBlock should store fourth byte correctly");
}

void test_cache_block_rejects_invalid_offsets_and_reset_clears_state() {
    CacheBlock<16> block;
    bool threw = false;
    try {
        block.write_word(2, 0xDEADBEEFu);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect_true(threw, "CacheBlock should reject unaligned word writes");

    threw = false;
    try {
        (void)block.read_byte(16);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect_true(threw, "CacheBlock should reject byte reads past the end of the block");

    block.set_tag(7);
    block.set_valid(true);
    block.set_dirty(true);
    block.write_word(0, 0xCAFEBABEu);
    block.reset();
    expect_equal(block.tag(), 0, "CacheBlock reset should clear the tag");
    expect_true(!block.valid(), "CacheBlock reset should clear the valid bit");
    expect_true(!block.dirty(), "CacheBlock reset should clear the dirty bit");
    expect_equal(block.read_word(0), 0u, "CacheBlock reset should zero stored bytes");
}

void test_standalone_l1_rejects_unsupported_store_modes() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteThrough,
        WriteMissPolicy::NoWriteAllocate,
        ReplacementPolicyType::LRU};

    L1Cache<kBlockSizeBytes> cache(config);
    bool threw = false;
    try {
        (void)cache.store(0x00, 0x12345678u);
    } catch (const std::logic_error&) {
        threw = true;
    }

    expect_true(threw,
                "Standalone L1Cache should reject store modes that require a backing hierarchy");
}

void test_l1_cache_validates_geometry_and_alignment() {
    bool threw = false;
    try {
        const CacheConfig invalid_geometry{
            48,
            2,
            WritePolicy::WriteBack,
            WriteMissPolicy::WriteAllocate,
            ReplacementPolicyType::LRU};
        L1Cache<16> cache(invalid_geometry);
        (void)cache;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect_true(threw, "L1Cache should reject cache sizes that are not multiples of block size");

    const CacheConfig valid_config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};
    L1Cache<16> cache(valid_config);
    uint32_t value = 0;
    threw = false;
    try {
        cache.load(0x02, value);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect_true(threw, "L1Cache should reject unaligned loads");
}

void test_cache_stats_accumulates_counters() {
    CacheStats stats;
    stats.record_read_hit();
    stats.record_read_miss();
    stats.record_write_hit();
    stats.record_write_miss();
    stats.record_writeback();

    expect_equal_u64(stats.read_hits(), 1, "CacheStats should count read hits");
    expect_equal_u64(stats.read_misses(), 1, "CacheStats should count read misses");
    expect_equal_u64(stats.write_hits(), 1, "CacheStats should count write hits");
    expect_equal_u64(stats.write_misses(), 1, "CacheStats should count write misses");
    expect_equal_u64(stats.writebacks(), 1, "CacheStats should count writebacks");
}

void test_cache_set_lookup_and_install() {
    constexpr uint32_t kBlockSizeBytes = 16;
    CacheSet<kBlockSizeBytes> set(2, create_replacement_policy(ReplacementPolicyType::LRU));

    expect_true(set.find_way_by_tag(0xA) == -1, "Empty set should not find a tag");

    const uint32_t victim_way = set.choose_victim_way();
    CacheBlockBase& block = set.install_block_at_way(victim_way, 0xA, 1);
    expect_equal(victim_way, 0, "First install should use the first invalid way");
    expect_true(block.valid(), "Installed block should be valid");
    expect_equal(block.tag(), 0xA, "Installed block should record tag");
    expect_equal(static_cast<uint32_t>(set.find_way_by_tag(0xA)), 0,
                 "Set should find installed tag");
}

void test_cache_set_rejects_null_replacement_policy() {
    bool threw = false;
    try {
        CacheSet<16> set(2, nullptr);
        (void)set;
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expect_true(threw, "CacheSet should reject null replacement policies");
}

void test_victim_cache_disabled_and_validation_behavior() {
    VictimCache<16> disabled_cache(VictimCacheConfig{false, 0, ReplacementPolicyType::LRU});
    uint32_t value = 0;
    CacheLineSnapshot<16> snapshot{};
    expect_true(!disabled_cache.enabled(), "Disabled victim cache should report disabled");
    expect_true(!disabled_cache.contains_line(0x00), "Disabled victim cache should not contain lines");
    expect_true(!disabled_cache.access_load(0x00, value), "Disabled victim cache should miss loads");
    expect_true(!disabled_cache.access_store(0x00, 0x1u, false),
                "Disabled victim cache should miss stores");
    expect_true(!disabled_cache.get_line_snapshot(0x00, snapshot),
                "Disabled victim cache should not provide snapshots");

    bool threw = false;
    try {
        VictimCache<16> invalid_cache(VictimCacheConfig{true, 0, ReplacementPolicyType::LRU});
        (void)invalid_cache;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect_true(threw, "Enabled victim cache should require a non-zero entry count");
}

void test_victim_cache_debug_print_reports_disabled_state() {
    VictimCache<16> cache(VictimCacheConfig{false, 0, ReplacementPolicyType::LRU});
    std::ostringstream os;
    cache.debug_print(os);
    expect_contains(os.str(), "disabled", "Victim-cache debug print should mention disabled state");
}

void test_trace_runner_executes_trace_file() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    const std::filesystem::path trace_path =
        std::filesystem::temp_directory_path() / "cache_trace_test.trace";
    {
        std::ofstream trace(trace_path);
        trace << "W 0x0 0x11223344\n";
        trace << "R 0x0\n";
        trace << "R 0x10\n";
    }

    L1Cache<kBlockSizeBytes> cache(config);
    TraceRunner<L1Cache<kBlockSizeBytes>> runner(cache);
    std::ostringstream output;
    const TraceResult result = runner.run_file(trace_path.string(), output);

    expect_equal_u64(result.operations, 3, "Trace runner should count operations");
    expect_equal_u64(result.load_operations, 2, "Trace runner should count loads");
    expect_equal_u64(result.store_operations, 1, "Trace runner should count stores");
    expect_equal_u64(cache.write_hits(), 0, "Trace store should miss on first access");
    expect_equal_u64(cache.write_misses(), 1, "Trace store miss should be counted");
    expect_equal_u64(cache.read_hits(), 1, "Trace should produce one read hit");
    expect_equal_u64(cache.read_misses(), 1, "Trace should produce one read miss");
    expect_equal_u64(cache.compulsory_misses(), 2, "Trace should classify compulsory misses");
    expect_equal_u64(cache.capacity_misses(), 0, "Trace should not classify capacity misses");
    expect_equal_u64(cache.conflict_misses(), 0, "Trace should not classify conflict misses");

    std::filesystem::remove(trace_path);
}

void test_trace_runner_rejects_extra_tokens() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    const std::filesystem::path trace_path =
        std::filesystem::temp_directory_path() / "cache_trace_invalid.trace";
    {
        std::ofstream trace(trace_path);
        trace << "R 0x0 extra_token\n";
    }

    L1Cache<kBlockSizeBytes> cache(config);
    TraceRunner<L1Cache<kBlockSizeBytes>> runner(cache);
    std::ostringstream output;

    bool threw = false;
    try {
        runner.run_file(trace_path.string(), output);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    std::filesystem::remove(trace_path);
    expect_true(threw, "Trace runner should reject trailing tokens");
}

void test_trace_runner_rejects_invalid_operation_and_missing_file() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    const std::filesystem::path invalid_trace_path =
        std::filesystem::temp_directory_path() / "cache_trace_invalid_opcode.trace";
    {
        std::ofstream trace(invalid_trace_path);
        trace << "X 0x0\n";
    }

    L1Cache<kBlockSizeBytes> cache(config);
    TraceRunner<L1Cache<kBlockSizeBytes>> runner(cache);
    std::ostringstream output;

    bool threw = false;
    try {
        runner.run_file(invalid_trace_path.string(), output);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    std::filesystem::remove(invalid_trace_path);
    expect_true(threw, "Trace runner should reject invalid opcodes");

    threw = false;
    try {
        runner.run_file("traces/does_not_exist.trace", output);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect_true(threw, "Trace runner should reject missing trace files");
}

void test_trace_runner_allows_inline_comments() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    const std::filesystem::path trace_path =
        std::filesystem::temp_directory_path() / "cache_trace_comments.trace";
    {
        std::ofstream trace(trace_path);
        trace << "W 0x0 0x1   # first store\n";
        trace << "R 0x0       # should hit\n";
    }

    L1Cache<kBlockSizeBytes> cache(config);
    TraceRunner<L1Cache<kBlockSizeBytes>> runner(cache);
    std::ostringstream output;
    const TraceResult result = runner.run_file(trace_path.string(), output);

    std::filesystem::remove(trace_path);
    expect_equal_u64(result.operations, 2, "Trace runner should ignore inline comments");
    expect_equal_u64(cache.read_hits(), 1, "Inline-comment trace should still execute");
}

void test_trace_runner_rejects_out_of_range_numeric_tokens() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    const std::filesystem::path trace_path =
        std::filesystem::temp_directory_path() / "cache_trace_oor.trace";
    {
        std::ofstream trace(trace_path);
        trace << "R 0x100000000\n";
    }

    L1Cache<kBlockSizeBytes> cache(config);
    TraceRunner<L1Cache<kBlockSizeBytes>> runner(cache);
    std::ostringstream output;
    bool threw = false;
    try {
        runner.run_file(trace_path.string(), output);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    std::filesystem::remove(trace_path);
    expect_true(threw, "Trace runner should reject numeric tokens outside uint32_t range");
}

void test_trace_runner_rejects_negative_numeric_tokens() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    const std::filesystem::path trace_path =
        std::filesystem::temp_directory_path() / "cache_trace_negative.trace";
    {
        std::ofstream trace(trace_path);
        trace << "W 0x0 -1\n";
    }

    L1Cache<kBlockSizeBytes> cache(config);
    TraceRunner<L1Cache<kBlockSizeBytes>> runner(cache);
    std::ostringstream output;
    bool threw = false;
    try {
        runner.run_file(trace_path.string(), output);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    std::filesystem::remove(trace_path);
    expect_true(threw, "Trace runner should reject negative numeric tokens");
}

void test_trace_runner_rejects_store_without_value() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const CacheConfig config{
        64,
        2,
        WritePolicy::WriteBack,
        WriteMissPolicy::WriteAllocate,
        ReplacementPolicyType::LRU};

    const std::filesystem::path trace_path =
        std::filesystem::temp_directory_path() / "cache_trace_missing_value.trace";
    {
        std::ofstream trace(trace_path);
        trace << "W 0x0\n";
    }

    L1Cache<kBlockSizeBytes> cache(config);
    TraceRunner<L1Cache<kBlockSizeBytes>> runner(cache);
    std::ostringstream output;
    bool threw = false;
    try {
        runner.run_file(trace_path.string(), output);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    std::filesystem::remove(trace_path);
    expect_true(threw, "Trace runner should reject store operations without a value");
}

void test_regression_scan_trace() {
    expect_trace_case<16>("traces/tiny/scan_trace.txt", 16, 0, 16, 8, 8, 0, "scan trace");
}

void test_regression_thrashing_trace() {
    expect_trace_case<16>("traces/tiny/thrashing_trace.txt", 6, 0, 6, 3, 0, 3, "thrashing trace");
}

void test_regression_recency_friendly_trace() {
    expect_trace_case<16>("traces/tiny/recency_friendly_trace.txt", 12, 8, 4, 4, 0, 0,
                          "recency-friendly trace");
}

void test_regression_streaming_trace() {
    expect_trace_case<16>("traces/tiny/streaming_trace.txt", 12, 0, 12, 12, 0, 0, "streaming trace");
}

void test_regression_mixed_access_trace() {
    expect_trace_case<16>("traces/tiny/mixed_access_pattern_trace.txt", 13, 7, 6, 5, 1, 0,
                          "mixed trace");
}

void test_inclusive_hierarchy_keeps_block_in_lower_levels() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{64, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    expect_true(!hierarchy.load(0x00, value), "First hierarchy load should miss");

    expect_true(hierarchy.l1().contains_line(0x00), "Inclusive hierarchy should install line in L1");
    expect_true(hierarchy.l2().contains_line(0x00), "Inclusive hierarchy should retain line in L2");
    expect_true(hierarchy.l3().contains_line(0x00), "Inclusive hierarchy should retain line in L3");
}

void test_exclusive_hierarchy_moves_lines_between_levels() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{64, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Exclusive};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    expect_true(!hierarchy.load(0x00, value), "First exclusive load should miss");
    expect_true(hierarchy.l1().contains_line(0x00), "Exclusive hierarchy should place new line in L1");
    expect_true(!hierarchy.l2().contains_line(0x00), "Exclusive hierarchy should not duplicate line in L2");
    expect_true(!hierarchy.l3().contains_line(0x00), "Exclusive hierarchy should not duplicate line in L3");

    expect_true(!hierarchy.load(0x20, value), "Second exclusive load should miss");
    expect_true(!hierarchy.load(0x40, value), "Third exclusive load should miss and evict one line from L1");

    expect_true(!hierarchy.l1().contains_line(0x00), "Evicted line should leave L1 in exclusive mode");
    expect_true(hierarchy.l2().contains_line(0x00), "Evicted line should demote to L2 in exclusive mode");

    expect_true(!hierarchy.load(0x00, value), "Reloading demoted line should miss in L1 and hit in L2");
    expect_true(hierarchy.l1().contains_line(0x00), "Reloaded line should return to L1");
    expect_true(!hierarchy.l2().contains_line(0x00), "Reloaded line should be removed from L2 in exclusive mode");
}

void test_noninclusive_hierarchy_does_not_force_invalidation() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::NonInclusiveNonExclusive};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    expect_true(!hierarchy.load(0x00, value), "First non-inclusive load should miss");
    expect_true(!hierarchy.load(0x10, value), "Second non-inclusive load should miss");
    expect_true(!hierarchy.load(0x20, value), "Third non-inclusive load should miss and evict from L3");

    expect_true(hierarchy.l1().contains_line(0x00),
                "Non-inclusive hierarchy should allow L1 to retain a line after lower eviction");
    expect_true(!hierarchy.l3().contains_line(0x00),
                "Non-inclusive hierarchy does not require L3 to retain the upper-level line");
}

void test_victim_cache_recovers_recent_l1_eviction() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive,
        VictimCacheConfig{true, 1, ReplacementPolicyType::LRU}};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    expect_true(!hierarchy.load(0x00, value), "First load should miss");
    expect_true(!hierarchy.load(0x20, value), "Second load should miss");
    expect_true(!hierarchy.load(0x40, value), "Third load should evict one line from L1");

    expect_true(!hierarchy.l1().contains_line(0x00),
                "Evicted L1 line should leave the primary cache");
    expect_true(hierarchy.victim_cache().contains_line(0x00),
                "Victim cache should capture the most recent L1 eviction");

    expect_true(!hierarchy.load(0x00, value),
                "Victim cache hit should still be an L1 miss overall");
    expect_true(hierarchy.l1().contains_line(0x00),
                "Victim cache hit should promote the line back into L1");
    expect_true(hierarchy.victim_cache().contains_line(0x20),
                "Victim cache should receive the displaced L1 line during the swap");
    expect_equal_u64(hierarchy.victim_cache().read_hits(), 1,
                     "Victim cache should record the rescue hit");
}

void test_victim_cache_eviction_demotes_in_exclusive_mode() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{64, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Exclusive,
        VictimCacheConfig{true, 1, ReplacementPolicyType::LRU}};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    expect_true(!hierarchy.load(0x00, value), "First exclusive load should miss");
    expect_true(!hierarchy.load(0x20, value), "Second exclusive load should miss");
    expect_true(!hierarchy.load(0x40, value), "Third exclusive load should place one line in victim cache");
    expect_true(!hierarchy.load(0x60, value),
                "Fourth exclusive load should force victim-cache eviction");

    expect_true(!hierarchy.l1().contains_line(0x00),
                "Oldest line should not remain in L1 after victim-cache overflow");
    expect_true(!hierarchy.victim_cache().contains_line(0x00),
                "Oldest line should have left the victim cache after overflow");
    expect_true(hierarchy.l2().contains_line(0x00),
                "Victim-cache eviction should demote the line into L2 in exclusive mode");
}

void test_hierarchy_stats_csv_export_contains_all_levels() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{64, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    hierarchy.load(0x00, value);

    const std::string csv = hierarchy.stats_csv();
    expect_true(csv.find("level,read_hits,read_misses") != std::string::npos,
                "CSV export should contain a header");
    expect_true(csv.find("L1,") != std::string::npos, "CSV export should contain an L1 row");
    expect_true(csv.find("L2,") != std::string::npos, "CSV export should contain an L2 row");
    expect_true(csv.find("L3,") != std::string::npos, "CSV export should contain an L3 row");
}

void test_hierarchy_stats_json_export_contains_all_levels() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{64, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    hierarchy.load(0x00, value);

    const std::string json = hierarchy.stats_json();
    expect_true(json.find("\"L1\"") != std::string::npos, "JSON export should contain L1");
    expect_true(json.find("\"L2\"") != std::string::npos, "JSON export should contain L2");
    expect_true(json.find("\"L3\"") != std::string::npos, "JSON export should contain L3");
    expect_true(json.find("\"read_misses\"") != std::string::npos,
                "JSON export should contain stat fields");
}

void test_hierarchy_stats_exports_include_victim_cache_when_enabled() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive,
        VictimCacheConfig{true, 1, ReplacementPolicyType::LRU}};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    hierarchy.load(0x00, value);
    hierarchy.load(0x20, value);
    hierarchy.load(0x40, value);
    hierarchy.load(0x00, value);

    const std::string csv = hierarchy.stats_csv();
    const std::string json = hierarchy.stats_json();
    expect_true(csv.find("\nVC,") != std::string::npos,
                "CSV export should include a victim-cache row when enabled");
    expect_true(json.find("\"VC\"") != std::string::npos,
                "JSON export should include a victim-cache object when enabled");
}

void test_hierarchy_debug_print_includes_victim_cache_section_when_enabled() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive,
        VictimCacheConfig{true, 1, ReplacementPolicyType::LRU}};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    std::ostringstream os;
    hierarchy.debug_print(os);
    expect_contains(os.str(), "=== VC ===",
                    "Hierarchy debug print should include a VC section when enabled");
}

void test_hierarchy_rejects_unsupported_writeback_no_write_allocate() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{64, 2, WritePolicy::WriteBack, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive};

    bool threw = false;
    try {
        CacheHierarchy<kBlockSizeBytes> hierarchy(config);
        (void)hierarchy;
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expect_true(threw, "Hierarchy should reject WriteBack + NoWriteAllocate");
}

void test_hierarchy_rejects_unsupported_writethrough_writeallocate() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{64, 2, WritePolicy::WriteThrough, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteThrough, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteThrough, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive};

    bool threw = false;
    try {
        CacheHierarchy<kBlockSizeBytes> hierarchy(config);
        (void)hierarchy;
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expect_true(threw, "Hierarchy should reject WriteThrough + WriteAllocate");
}

void test_hierarchy_rejects_mixed_write_modes_across_levels() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{64, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Exclusive};

    bool threw = false;
    try {
        CacheHierarchy<kBlockSizeBytes> hierarchy(config);
        (void)hierarchy;
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expect_true(threw, "Hierarchy should reject mixed write modes across levels");
}

void test_writethrough_noallocate_store_miss_updates_memory_without_allocating() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{64, 2, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::NonInclusiveNonExclusive};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    expect_true(!hierarchy.store(0x80, 0xAABBCCDDu),
                "Write-through no-write-allocate store miss should report miss");
    expect_true(!hierarchy.l1().contains_line(0x80),
                "Write-through no-write-allocate should not allocate in L1");
    expect_true(!hierarchy.l2().contains_line(0x80),
                "Write-through no-write-allocate should not allocate in L2");
    expect_true(!hierarchy.l3().contains_line(0x80),
                "Write-through no-write-allocate should not allocate in L3");

    uint32_t value = 0;
    expect_true(!hierarchy.load(0x80, value),
                "Subsequent load should miss in caches and fetch updated memory value");
    expect_equal(value, 0xAABBCCDDu,
                 "Write-through no-write-allocate store should update backing memory");
}

void test_writethrough_hit_propagates_to_lower_levels() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{64, 2, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    expect_true(!hierarchy.load(0x00, value), "First load should fill hierarchy");
    expect_true(hierarchy.store(0x00, 0x12345678u),
                "Write-through store hit should hit in L1");

    L1Cache<kBlockSizeBytes>::LineSnapshot snapshot{};
    expect_true(hierarchy.l2().get_line_snapshot(0x00, snapshot),
                "Inclusive write-through should keep line in L2");
    expect_equal(read_word_from_snapshot_bytes(snapshot, 0), 0x12345678u,
                 "Write-through hit should update L2 copy");
    expect_true(hierarchy.l3().get_line_snapshot(0x00, snapshot),
                "Inclusive write-through should keep line in L3");
    expect_equal(read_word_from_snapshot_bytes(snapshot, 0), 0x12345678u,
                 "Write-through hit should update L3 copy");

    expect_true(!hierarchy.load(0x20, value), "Load should miss and fill a second line");
    expect_true(!hierarchy.load(0x40, value), "Load should miss and evict from L1");
    expect_true(!hierarchy.load(0x00, value), "Reload should miss in L1 but hit lower with updated value");
    expect_equal(value, 0x12345678u,
                 "Write-through hit should propagate updated value beyond L1");
}

void test_writethrough_victim_cache_hit_stays_clean() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteThrough, WriteMissPolicy::NoWriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive,
        VictimCacheConfig{true, 1, ReplacementPolicyType::LRU}};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    uint32_t value = 0;
    expect_true(!hierarchy.load(0x00, value), "First load should miss");
    expect_true(!hierarchy.load(0x20, value), "Second load should miss");
    expect_true(!hierarchy.load(0x40, value), "Third load should evict the first line into VC");
    expect_true(hierarchy.victim_cache().contains_line(0x00),
                "Victim cache should hold the evicted line before the store");

    expect_true(!hierarchy.store(0x00, 0xCAFEBABEu),
                "Write-through store rescued from victim cache should still report L1 miss");

    L1Cache<kBlockSizeBytes>::LineSnapshot snapshot{};
    expect_true(hierarchy.l1().get_line_snapshot(0x00, snapshot),
                "Victim-cache rescue should promote the line back into L1");
    expect_equal(read_word_from_snapshot_bytes(snapshot, 0), 0xCAFEBABEu,
                 "Victim-cache rescue should update the value in L1");
    expect_true(!snapshot.dirty,
                "Write-through victim-cache rescue should leave the L1 line clean");

    expect_true(hierarchy.l2().get_line_snapshot(0x00, snapshot),
                "Inclusive write-through should keep the line in L2");
    expect_true(!snapshot.dirty,
                "Write-through victim-cache rescue should leave the L2 line clean");
    expect_true(hierarchy.l3().get_line_snapshot(0x00, snapshot),
                "Inclusive write-through should keep the line in L3");
    expect_true(!snapshot.dirty,
                "Write-through victim-cache rescue should leave the L3 line clean");
}

void test_inclusive_dirty_cascade_keeps_levels_consistent() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    expect_true(!hierarchy.store(0x00, 1), "First store should miss");
    expect_true(!hierarchy.store(0x20, 2), "Second store should miss");
    expect_true(!hierarchy.store(0x40, 3), "Third store should miss and force cascaded evictions");

    expect_true(!hierarchy.l1().contains_line(0x00),
                "Inclusive hierarchy should invalidate L1 when lower levels evict a line");
    expect_true(!hierarchy.l2().contains_line(0x00),
                "Inclusive hierarchy should invalidate L2 when L3 evicts a line");
    expect_true(!hierarchy.l3().contains_line(0x00),
                "L3 should no longer contain the evicted line");
}

void test_inclusive_victim_cache_preserves_dirty_data_on_lower_eviction() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive,
        VictimCacheConfig{true, 2, ReplacementPolicyType::LRU}};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    expect_true(!hierarchy.store(0x00, 0x11112222u), "First store should miss");
    expect_true(!hierarchy.store(0x20, 0x33334444u), "Second store should miss");
    expect_true(!hierarchy.store(0x40, 0x55556666u),
                "Third store should evict the oldest dirty line from lower levels");

    uint32_t value = 0;
    expect_true(!hierarchy.load(0x00, value),
                "Reload should miss in upper levels after inclusive lower eviction");
    expect_equal(value, 0x11112222u,
                 "Inclusive eviction should preserve the newest dirty data via writeback");
}

void test_hierarchy_writebacks_include_victim_cache_writebacks() {
    constexpr uint32_t kBlockSizeBytes = 16;
    const HierarchyConfig config{
        CacheConfig{32, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{128, 2, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        CacheConfig{256, 4, WritePolicy::WriteBack, WriteMissPolicy::WriteAllocate,
                    ReplacementPolicyType::LRU},
        InclusionPolicy::Inclusive,
        VictimCacheConfig{true, 1, ReplacementPolicyType::LRU}};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    expect_true(!hierarchy.store(0x00, 0x01020304u), "First store should miss");
    expect_true(!hierarchy.store(0x20, 0x11121314u), "Second store should miss");
    expect_true(!hierarchy.store(0x40, 0x21222324u), "Third store should miss");
    expect_true(!hierarchy.store(0x60, 0x31323334u),
                "Fourth store should overflow the victim cache");

    expect_true(hierarchy.victim_cache().writebacks() > 0,
                "Victim cache overflow should record a victim-cache writeback");
    expect_true(hierarchy.writebacks() >= hierarchy.victim_cache().writebacks(),
                "Hierarchy writebacks should include victim-cache writebacks");
}

}  // namespace

int main() {
    const std::vector<void (*)()> tests = {
        test_lru_policy_prefers_least_recently_used,
        test_create_replacement_policy_returns_instances_for_all_supported_types,
        test_fifo_policy_prefers_oldest_insertion,
        test_random_policy_is_deterministic_and_uses_empty_way_first,
        test_l1_cache_fifo_eviction_behavior,
        test_l1_cache_random_policy_wires_through_config,
        test_l1_cache_lru_eviction_behavior,
        test_l1_cache_counts_writebacks_on_dirty_eviction,
        test_cache_block_word_and_byte_access_round_trip,
        test_cache_block_rejects_invalid_offsets_and_reset_clears_state,
        test_standalone_l1_rejects_unsupported_store_modes,
        test_l1_cache_validates_geometry_and_alignment,
        test_cache_stats_accumulates_counters,
        test_cache_set_lookup_and_install,
        test_cache_set_rejects_null_replacement_policy,
        test_victim_cache_disabled_and_validation_behavior,
        test_victim_cache_debug_print_reports_disabled_state,
        test_trace_runner_executes_trace_file,
        test_trace_runner_rejects_extra_tokens,
        test_trace_runner_rejects_invalid_operation_and_missing_file,
        test_trace_runner_allows_inline_comments,
        test_trace_runner_rejects_out_of_range_numeric_tokens,
        test_trace_runner_rejects_negative_numeric_tokens,
        test_trace_runner_rejects_store_without_value,
        test_regression_scan_trace,
        test_regression_thrashing_trace,
        test_regression_recency_friendly_trace,
        test_regression_streaming_trace,
        test_regression_mixed_access_trace,
        test_inclusive_hierarchy_keeps_block_in_lower_levels,
        test_exclusive_hierarchy_moves_lines_between_levels,
        test_noninclusive_hierarchy_does_not_force_invalidation,
        test_victim_cache_recovers_recent_l1_eviction,
        test_victim_cache_eviction_demotes_in_exclusive_mode,
        test_hierarchy_stats_csv_export_contains_all_levels,
        test_hierarchy_stats_json_export_contains_all_levels,
        test_hierarchy_stats_exports_include_victim_cache_when_enabled,
        test_hierarchy_debug_print_includes_victim_cache_section_when_enabled,
        test_hierarchy_rejects_unsupported_writeback_no_write_allocate,
        test_hierarchy_rejects_unsupported_writethrough_writeallocate,
        test_hierarchy_rejects_mixed_write_modes_across_levels,
        test_writethrough_noallocate_store_miss_updates_memory_without_allocating,
        test_writethrough_hit_propagates_to_lower_levels,
        test_writethrough_victim_cache_hit_stays_clean,
        test_inclusive_dirty_cascade_keeps_levels_consistent,
        test_inclusive_victim_cache_preserves_dirty_data_on_lower_eviction,
        test_hierarchy_writebacks_include_victim_cache_writebacks,
    };

    for (const auto test : tests) {
        test();
    }

    std::cout << "All tests passed\n";
    return 0;
}
