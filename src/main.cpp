#include "cache_simulator/cache_hierarchy.hpp"
#include "cache_simulator/trace_runner.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>

namespace {

InclusionPolicy parse_inclusion_policy(const std::string& value) {
    if (value == "inclusive") {
        return InclusionPolicy::Inclusive;
    }
    if (value == "exclusive") {
        return InclusionPolicy::Exclusive;
    }
    if (value == "non-inclusive" || value == "noninclusive" ||
        value == "non-inclusive-non-exclusive") {
        return InclusionPolicy::NonInclusiveNonExclusive;
    }
    throw std::invalid_argument(
        "Inclusion policy must be 'inclusive', 'exclusive', or 'non-inclusive'");
}

struct WritePolicySelection {
    WritePolicy write_policy;
    WriteMissPolicy write_miss_policy;
};

WritePolicySelection parse_write_policy_selection(const std::string& value) {
    if (value == "wb" || value == "write-back" || value == "writeback") {
        return WritePolicySelection{
            WritePolicy::WriteBack,
            WriteMissPolicy::WriteAllocate,
        };
    }
    if (value == "wt" || value == "write-through" || value == "writethrough") {
        return WritePolicySelection{
            WritePolicy::WriteThrough,
            WriteMissPolicy::NoWriteAllocate,
        };
    }
    throw std::invalid_argument(
        "Write policy must be 'write-back'/'wb' or 'write-through'/'wt'");
}

ReplacementPolicyType parse_replacement_policy(const std::string& value) {
    if (value == "lru") {
        return ReplacementPolicyType::LRU;
    }
    if (value == "fifo") {
        return ReplacementPolicyType::FIFO;
    }
    if (value == "random") {
        return ReplacementPolicyType::Random;
    }
    throw std::invalid_argument("Replacement policy must be 'lru', 'fifo', or 'random'");
}

bool is_export_format(const std::string& value) {
    return value == "csv" || value == "json";
}

bool is_write_policy_value(const std::string& value) {
    return value == "wb" || value == "write-back" || value == "writeback" ||
           value == "wt" || value == "write-through" || value == "writethrough";
}

struct VictimCacheSelection {
    bool enabled = false;
    uint32_t entries = 0;
    ReplacementPolicyType replacement_policy = ReplacementPolicyType::LRU;
};

VictimCacheSelection parse_victim_cache_selection(const std::string& value) {
    constexpr char kPrefix[] = "vc=";
    if (value.rfind(kPrefix, 0) != 0) {
        throw std::invalid_argument(
            "Victim cache option must be 'vc=off', 'vc=<entries>', or 'vc=<entries>:<policy>'");
    }

    const std::string payload = value.substr(3);
    if (payload == "off") {
        return VictimCacheSelection{};
    }

    const std::size_t separator = payload.find(':');
    const std::string entries_token =
        separator == std::string::npos ? payload : payload.substr(0, separator);
    if (entries_token.empty()) {
        throw std::invalid_argument("Victim cache entries must be provided in vc option");
    }

    std::size_t parsed_chars = 0;
    const unsigned long parsed_entries = std::stoul(entries_token, &parsed_chars, 10);
    if (parsed_chars != entries_token.size() || parsed_entries == 0) {
        throw std::invalid_argument("Victim cache entries must be a positive integer");
    }

    VictimCacheSelection selection;
    selection.enabled = true;
    selection.entries = static_cast<uint32_t>(parsed_entries);
    if (separator != std::string::npos) {
        selection.replacement_policy = parse_replacement_policy(payload.substr(separator + 1));
    }
    return selection;
}

std::string victim_cache_policy_label(ReplacementPolicyType policy) {
    switch (policy) {
        case ReplacementPolicyType::LRU:
            return "LRU";
        case ReplacementPolicyType::FIFO:
            return "FIFO";
        case ReplacementPolicyType::Random:
            return "Random";
    }
    throw std::invalid_argument("Unknown replacement policy");
}

template <uint32_t BlockSizeBytes>
void print_hierarchy_summary(const CacheHierarchy<BlockSizeBytes>& hierarchy) {
    std::cout << "L1 read_hits=" << hierarchy.l1().read_hits()
              << " read_misses=" << hierarchy.l1().read_misses()
              << " write_hits=" << hierarchy.l1().write_hits()
              << " write_misses=" << hierarchy.l1().write_misses()
              << " writebacks=" << hierarchy.l1().writebacks()
              << " compulsory_misses=" << hierarchy.l1().compulsory_misses()
              << " capacity_misses=" << hierarchy.l1().capacity_misses()
              << " conflict_misses=" << hierarchy.l1().conflict_misses() << "\n";
    if (hierarchy.victim_cache().enabled()) {
        std::cout << "VC read_hits=" << hierarchy.victim_cache().read_hits()
                  << " read_misses=" << hierarchy.victim_cache().read_misses()
                  << " write_hits=" << hierarchy.victim_cache().write_hits()
                  << " write_misses=" << hierarchy.victim_cache().write_misses()
                  << " writebacks=" << hierarchy.victim_cache().writebacks() << "\n";
    }
    std::cout << "L2 read_hits=" << hierarchy.l2().read_hits()
              << " read_misses=" << hierarchy.l2().read_misses()
              << " write_hits=" << hierarchy.l2().write_hits()
              << " write_misses=" << hierarchy.l2().write_misses()
              << " writebacks=" << hierarchy.l2().writebacks() << "\n";
    std::cout << "L3 read_hits=" << hierarchy.l3().read_hits()
              << " read_misses=" << hierarchy.l3().read_misses()
              << " write_hits=" << hierarchy.l3().write_hits()
              << " write_misses=" << hierarchy.l3().write_misses()
              << " writebacks=" << hierarchy.l3().writebacks() << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        constexpr uint32_t kL1BlockSizeBytes = 16;

        InclusionPolicy inclusion_policy = InclusionPolicy::Inclusive;
        WritePolicySelection write_policy_selection{
            WritePolicy::WriteBack,
            WriteMissPolicy::WriteAllocate,
        };
        VictimCacheSelection victim_cache_selection{};
        std::string export_format;

        if (argc > 2) {
            inclusion_policy = parse_inclusion_policy(argv[2]);
        }

        for (int arg_index = 3; arg_index < argc; ++arg_index) {
            const std::string arg = argv[arg_index];
            if (is_export_format(arg)) {
                if (!export_format.empty()) {
                    throw std::invalid_argument("Export format may only be specified once");
                }
                export_format = arg;
                continue;
            }
            if (is_write_policy_value(arg)) {
                write_policy_selection = parse_write_policy_selection(arg);
                continue;
            }
            if (arg.rfind("vc=", 0) == 0) {
                victim_cache_selection = parse_victim_cache_selection(arg);
                continue;
            }
            throw std::invalid_argument(
                "Unrecognized option. Supported options are write-back/write-through, "
                "csv/json, and vc=off|vc=<entries>[:lru|fifo|random]");
        }

        const HierarchyConfig config{
            /*l1=*/CacheConfig{64, 2, write_policy_selection.write_policy,
                               write_policy_selection.write_miss_policy, ReplacementPolicyType::LRU},
            /*l2=*/CacheConfig{128, 2, write_policy_selection.write_policy,
                               write_policy_selection.write_miss_policy, ReplacementPolicyType::LRU},
            /*l3=*/CacheConfig{256, 4, write_policy_selection.write_policy,
                               write_policy_selection.write_miss_policy, ReplacementPolicyType::LRU},
            /*inclusion_policy=*/inclusion_policy,
            /*victim_cache=*/VictimCacheConfig{victim_cache_selection.enabled,
                                               victim_cache_selection.entries,
                                               victim_cache_selection.replacement_policy}};

        CacheHierarchy<kL1BlockSizeBytes> hierarchy(config);

        if (argc > 1) {
            TraceRunner<CacheHierarchy<kL1BlockSizeBytes>> trace_runner(hierarchy);
            const TraceResult result = trace_runner.run_file(argv[1], std::cout);

            std::cout << "\noperations=" << result.operations
                      << " loads=" << result.load_operations
                      << " stores=" << result.store_operations << "\n";
            if (hierarchy.victim_cache().enabled()) {
                std::cout << "VC entries=" << hierarchy.victim_cache().entries()
                          << " replacement="
                          << victim_cache_policy_label(config.victim_cache.replacement_policy)
                          << "\n";
            }
            print_hierarchy_summary(hierarchy);
            if (export_format == "csv") {
                std::cout << "\n" << hierarchy.stats_csv() << "\n";
            } else if (export_format == "json") {
                std::cout << "\n" << hierarchy.stats_json() << "\n";
            } else if (!export_format.empty()) {
                throw std::invalid_argument("Export format must be 'csv' or 'json'");
            }
            return 0;
        }

        auto do_load = [&](uint32_t addr) {
            uint32_t value = 0;
            const bool hit = hierarchy.load(addr, value);
            std::cout << "load  0x" << std::hex << addr << std::dec
                      << " -> " << value
                      << (hit ? " (hit)\n" : " (miss)\n");
        };

        auto do_store = [&](uint32_t addr, uint32_t value) {
            const bool hit = hierarchy.store(addr, value);
            std::cout << "store 0x" << std::hex << addr << std::dec
                      << " <- " << value
                      << (hit ? " (hit)\n" : " (miss)\n");
        };

        do_load(0x00);
        do_load(0x04);
        do_store(0x00, 100);
        do_load(0x00);
        do_store(0x20, 200);
        do_load(0x20);
        do_load(0x40);
        do_load(0x60);

        std::cout << "\n";
        if (hierarchy.victim_cache().enabled()) {
            std::cout << "VC entries=" << hierarchy.victim_cache().entries()
                      << " replacement="
                      << victim_cache_policy_label(config.victim_cache.replacement_policy)
                      << "\n";
        }
        print_hierarchy_summary(hierarchy);
        if (export_format == "csv") {
            std::cout << "\n" << hierarchy.stats_csv() << "\n";
        } else if (export_format == "json") {
            std::cout << "\n" << hierarchy.stats_json() << "\n";
        } else if (!export_format.empty()) {
            throw std::invalid_argument("Export format must be 'csv' or 'json'");
        }

        std::cout << "\n";
        hierarchy.debug_print(std::cout);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
