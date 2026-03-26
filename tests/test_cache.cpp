#include "cache_hierarchy.hpp"
#include "cache_set.hpp"
#include "cache_stats.hpp"
#include "l1_cache.hpp"
#include "replacement_policy.hpp"
#include "trace_runner.hpp"

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

void test_regression_scan_trace() {
    expect_trace_case<16>("traces/scan_trace.txt", 16, 0, 16, 8, 8, 0, "scan trace");
}

void test_regression_thrashing_trace() {
    expect_trace_case<16>("traces/thrashing_trace.txt", 6, 0, 6, 3, 0, 3, "thrashing trace");
}

void test_regression_recency_friendly_trace() {
    expect_trace_case<16>("traces/recency_friendly_trace.txt", 12, 8, 4, 4, 0, 0,
                          "recency-friendly trace");
}

void test_regression_streaming_trace() {
    expect_trace_case<16>("traces/streaming_trace.txt", 12, 0, 12, 12, 0, 0, "streaming trace");
}

void test_regression_mixed_access_trace() {
    expect_trace_case<16>("traces/mixed_access_pattern_trace.txt", 13, 7, 6, 5, 1, 0,
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

}  // namespace

int main() {
    const std::vector<void (*)()> tests = {
        test_lru_policy_prefers_least_recently_used,
        test_fifo_policy_prefers_oldest_insertion,
        test_random_policy_is_deterministic_and_uses_empty_way_first,
        test_l1_cache_fifo_eviction_behavior,
        test_l1_cache_random_policy_wires_through_config,
        test_l1_cache_lru_eviction_behavior,
        test_l1_cache_counts_writebacks_on_dirty_eviction,
        test_standalone_l1_rejects_unsupported_store_modes,
        test_cache_stats_accumulates_counters,
        test_cache_set_lookup_and_install,
        test_trace_runner_executes_trace_file,
        test_trace_runner_rejects_extra_tokens,
        test_trace_runner_allows_inline_comments,
        test_trace_runner_rejects_out_of_range_numeric_tokens,
        test_trace_runner_rejects_negative_numeric_tokens,
        test_regression_scan_trace,
        test_regression_thrashing_trace,
        test_regression_recency_friendly_trace,
        test_regression_streaming_trace,
        test_regression_mixed_access_trace,
        test_inclusive_hierarchy_keeps_block_in_lower_levels,
        test_exclusive_hierarchy_moves_lines_between_levels,
        test_noninclusive_hierarchy_does_not_force_invalidation,
        test_hierarchy_stats_csv_export_contains_all_levels,
        test_hierarchy_stats_json_export_contains_all_levels,
        test_hierarchy_rejects_unsupported_writeback_no_write_allocate,
        test_hierarchy_rejects_unsupported_writethrough_writeallocate,
        test_hierarchy_rejects_mixed_write_modes_across_levels,
        test_writethrough_noallocate_store_miss_updates_memory_without_allocating,
        test_writethrough_hit_propagates_to_lower_levels,
        test_inclusive_dirty_cascade_keeps_levels_consistent,
    };

    for (const auto test : tests) {
        test();
    }

    std::cout << "All tests passed\n";
    return 0;
}
