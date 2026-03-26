#include "cache_simulator/cache_hierarchy.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr uint32_t kBlockSizeBytes = 16;

uint32_t compose_address(std::mt19937& rng) {
    const uint32_t block = rng() % 64;
    const uint32_t word = rng() % 4;
    return block * kBlockSizeBytes + word * 4;
}

ReplacementPolicyType random_policy(std::mt19937& rng) {
    switch (rng() % 3) {
        case 0:
            return ReplacementPolicyType::LRU;
        case 1:
            return ReplacementPolicyType::FIFO;
        default:
            return ReplacementPolicyType::Random;
    }
}

InclusionPolicy random_inclusion(std::mt19937& rng) {
    switch (rng() % 3) {
        case 0:
            return InclusionPolicy::Inclusive;
        case 1:
            return InclusionPolicy::Exclusive;
        default:
            return InclusionPolicy::NonInclusiveNonExclusive;
    }
}

template <typename CacheType>
bool snapshot_if_present(const CacheType& cache,
                         uint32_t byte_address,
                         CacheLineSnapshot<kBlockSizeBytes>& snapshot) {
    return cache.get_line_snapshot(byte_address, snapshot);
}

std::string describe_snapshot(const CacheLineSnapshot<kBlockSizeBytes>& snapshot,
                              uint32_t byte_offset) {
    std::ostringstream os;
    os << "{dirty=" << snapshot.dirty
       << ", word=0x"
       << std::hex
       << (static_cast<uint32_t>(snapshot.data[byte_offset]) |
           (static_cast<uint32_t>(snapshot.data[byte_offset + 1]) << 8) |
           (static_cast<uint32_t>(snapshot.data[byte_offset + 2]) << 16) |
           (static_cast<uint32_t>(snapshot.data[byte_offset + 3]) << 24))
       << "}";
    return os.str();
}

[[noreturn]] void fail(uint32_t seed,
                       uint32_t iteration,
                       const std::string& message) {
    std::ostringstream os;
    os << "Fuzz failure seed=" << seed
       << " iteration=" << iteration
       << " message=" << message;
    throw std::runtime_error(os.str());
}

std::string policy_name(ReplacementPolicyType policy) {
    switch (policy) {
        case ReplacementPolicyType::LRU:
            return "LRU";
        case ReplacementPolicyType::FIFO:
            return "FIFO";
        case ReplacementPolicyType::Random:
            return "Random";
    }
    return "Unknown";
}

std::string write_policy_name(WritePolicy policy) {
    return policy == WritePolicy::WriteThrough ? "WriteThrough" : "WriteBack";
}

std::string write_miss_policy_name(WriteMissPolicy policy) {
    return policy == WriteMissPolicy::NoWriteAllocate ? "NoWriteAllocate" : "WriteAllocate";
}

std::string inclusion_name(InclusionPolicy policy) {
    switch (policy) {
        case InclusionPolicy::Inclusive:
            return "Inclusive";
        case InclusionPolicy::Exclusive:
            return "Exclusive";
        case InclusionPolicy::NonInclusiveNonExclusive:
            return "NonInclusiveNonExclusive";
    }
    return "Unknown";
}

void verify_invariants(const CacheHierarchy<kBlockSizeBytes>& hierarchy,
                       const std::unordered_set<uint32_t>& touched_blocks,
                       InclusionPolicy inclusion_policy,
                       WritePolicy write_policy,
                       uint32_t seed,
                       uint32_t iteration) {
    for (uint32_t block : touched_blocks) {
        const uint32_t byte_address = block * kBlockSizeBytes;
        const bool in_l1 = hierarchy.l1().contains_line(byte_address);
        const bool in_vc = hierarchy.victim_cache().enabled() &&
                           hierarchy.victim_cache().contains_line(byte_address);
        const bool in_l2 = hierarchy.l2().contains_line(byte_address);
        const bool in_l3 = hierarchy.l3().contains_line(byte_address);

        if (in_l1 && in_vc) {
            fail(seed, iteration, "Line present in both L1 and victim cache");
        }

        if (inclusion_policy == InclusionPolicy::Inclusive) {
            if ((in_l1 || in_vc) && !in_l2) {
                fail(seed, iteration, "Inclusive hierarchy lost line in L2");
            }
            if ((in_l1 || in_vc || in_l2) && !in_l3) {
                fail(seed, iteration, "Inclusive hierarchy lost line in L3");
            }
        }

        if (inclusion_policy == InclusionPolicy::Exclusive) {
            const uint32_t residency_count =
                static_cast<uint32_t>(in_l1) +
                static_cast<uint32_t>(in_vc) +
                static_cast<uint32_t>(in_l2) +
                static_cast<uint32_t>(in_l3);
            if (residency_count > 1) {
                fail(seed, iteration, "Exclusive hierarchy duplicated a line across levels");
            }
        }

        if (write_policy == WritePolicy::WriteThrough) {
            CacheLineSnapshot<kBlockSizeBytes> snapshot{};
            if (snapshot_if_present(hierarchy.l1(), byte_address, snapshot) && snapshot.dirty) {
                fail(seed, iteration, "Write-through left a dirty line in L1");
            }
            if (hierarchy.victim_cache().enabled() &&
                snapshot_if_present(hierarchy.victim_cache(), byte_address, snapshot) &&
                snapshot.dirty) {
                fail(seed, iteration, "Write-through left a dirty line in victim cache");
            }
            if (snapshot_if_present(hierarchy.l2(), byte_address, snapshot) && snapshot.dirty) {
                fail(seed, iteration, "Write-through left a dirty line in L2");
            }
            if (snapshot_if_present(hierarchy.l3(), byte_address, snapshot) && snapshot.dirty) {
                fail(seed, iteration, "Write-through left a dirty line in L3");
            }
        }
    }
}

void run_case(uint32_t seed, uint32_t operations_per_seed) {
    std::mt19937 rng(seed);
    const bool use_write_through = (rng() % 2) == 0;
    const WritePolicy write_policy =
        use_write_through ? WritePolicy::WriteThrough : WritePolicy::WriteBack;
    const WriteMissPolicy write_miss_policy =
        use_write_through ? WriteMissPolicy::NoWriteAllocate : WriteMissPolicy::WriteAllocate;
    const InclusionPolicy inclusion_policy = random_inclusion(rng);
    const VictimCacheConfig victim_cache_config{
        (rng() % 2) == 0,
        1 + static_cast<uint32_t>(rng() % 4),
        random_policy(rng)};

    const HierarchyConfig config{
        CacheConfig{64, 2, write_policy, write_miss_policy, random_policy(rng)},
        CacheConfig{128, 2, write_policy, write_miss_policy, random_policy(rng)},
        CacheConfig{256, 4, write_policy, write_miss_policy, random_policy(rng)},
        inclusion_policy,
        victim_cache_config};

    CacheHierarchy<kBlockSizeBytes> hierarchy(config);
    std::unordered_map<uint32_t, uint32_t> shadow_memory;
    std::unordered_set<uint32_t> touched_blocks;
    std::vector<std::string> history;
    history.reserve(operations_per_seed);

    for (uint32_t iteration = 0; iteration < operations_per_seed; ++iteration) {
        const uint32_t address = compose_address(rng);
        touched_blocks.insert(address / kBlockSizeBytes);

        if ((rng() & 1u) == 0u) {
            uint32_t value = 0;
            hierarchy.load(address, value);
            const auto it = shadow_memory.find(address);
            const uint32_t expected = it == shadow_memory.end() ? 0u : it->second;
            std::ostringstream history_entry;
            history_entry << "R 0x" << std::hex << address
                          << " -> 0x" << value
                          << " expected=0x" << expected;
            history.push_back(history_entry.str());
            if (value != expected) {
                std::ostringstream os;
                os << "Load mismatch at address=0x" << std::hex << address
                   << " expected=0x" << expected
                   << " actual=0x" << value
                   << " config={inclusion=" << inclusion_name(inclusion_policy)
                   << ", write_policy=" << write_policy_name(write_policy)
                   << ", write_miss_policy=" << write_miss_policy_name(write_miss_policy)
                   << ", l1=" << policy_name(config.l1.replacement_policy)
                   << ", l2=" << policy_name(config.l2.replacement_policy)
                   << ", l3=" << policy_name(config.l3.replacement_policy)
                   << ", vc_enabled=" << config.victim_cache.enabled
                   << ", vc_entries=" << config.victim_cache.entries
                   << ", vc_policy=" << policy_name(config.victim_cache.replacement_policy)
                   << "} recent_history=[";
                const std::size_t start =
                    history.size() > 12 ? history.size() - 12 : 0;
                for (std::size_t i = start; i < history.size(); ++i) {
                    if (i != start) {
                        os << "; ";
                    }
                    os << history[i];
                }
                os << "] residency={";
                CacheLineSnapshot<kBlockSizeBytes> snapshot{};
                const uint32_t offset = address & (kBlockSizeBytes - 1);
                if (snapshot_if_present(hierarchy.l1(), address, snapshot)) {
                    os << "L1=" << describe_snapshot(snapshot, offset) << " ";
                }
                if (hierarchy.victim_cache().enabled() &&
                    snapshot_if_present(hierarchy.victim_cache(), address, snapshot)) {
                    os << "VC=" << describe_snapshot(snapshot, offset) << " ";
                }
                if (snapshot_if_present(hierarchy.l2(), address, snapshot)) {
                    os << "L2=" << describe_snapshot(snapshot, offset) << " ";
                }
                if (snapshot_if_present(hierarchy.l3(), address, snapshot)) {
                    os << "L3=" << describe_snapshot(snapshot, offset) << " ";
                }
                os << "}";
                fail(seed, iteration, os.str());
            }
        } else {
            const uint32_t value = rng();
            hierarchy.store(address, value);
            shadow_memory[address] = value;
            std::ostringstream history_entry;
            history_entry << "W 0x" << std::hex << address << " <- 0x" << value;
            history.push_back(history_entry.str());
        }

        verify_invariants(hierarchy,
                          touched_blocks,
                          inclusion_policy,
                          write_policy,
                          seed,
                          iteration);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const uint32_t seeds = argc > 1 ? static_cast<uint32_t>(std::stoul(argv[1])) : 64u;
    const uint32_t operations_per_seed =
        argc > 2 ? static_cast<uint32_t>(std::stoul(argv[2])) : 256u;

    for (uint32_t seed_index = 0; seed_index < seeds; ++seed_index) {
        run_case(0xC0FFEEu + seed_index, operations_per_seed);
    }

    std::cout << "Fuzz passed for " << seeds
              << " seeds x " << operations_per_seed << " operations\n";
    return 0;
}
