#ifndef CACHE_CONFIG_HPP
#define CACHE_CONFIG_HPP

#include <cstdint>

enum class WritePolicy {
    WriteBack,
    WriteThrough
};

enum class WriteMissPolicy {
    WriteAllocate,
    NoWriteAllocate
};

enum class ReplacementPolicyType {
    LRU,
    FIFO,
    Random
};

enum class InclusionPolicy {
    Inclusive,
    Exclusive,
    NonInclusiveNonExclusive
};

struct CacheConfig {
    uint32_t cache_size_bytes;
    uint32_t ways;
    WritePolicy write_policy;
    WriteMissPolicy write_miss_policy;
    ReplacementPolicyType replacement_policy;
};

struct HierarchyConfig {
    CacheConfig l1;
    CacheConfig l2;
    CacheConfig l3;
    InclusionPolicy inclusion_policy;
};

#endif
