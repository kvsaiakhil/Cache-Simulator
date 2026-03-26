#include "cache_simulator/cache_hierarchy.hpp"
#include "cache_simulator/trace_runner.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <stdexcept>

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

bool is_export_format(const std::string& value) {
    return value == "csv" || value == "json";
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
        std::string export_format;

        if (argc > 2) {
            inclusion_policy = parse_inclusion_policy(argv[2]);
        }

        if (argc > 3) {
            const std::string third_arg = argv[3];
            if (is_export_format(third_arg)) {
                export_format = third_arg;
            } else {
                write_policy_selection = parse_write_policy_selection(third_arg);
            }
        }

        if (argc > 4) {
            const std::string fourth_arg = argv[4];
            if (!export_format.empty()) {
                write_policy_selection = parse_write_policy_selection(fourth_arg);
            } else if (is_export_format(fourth_arg)) {
                export_format = fourth_arg;
            } else {
                throw std::invalid_argument(
                    "Fourth argument must be 'csv' or 'json' when third argument is a write policy");
            }
        }

        const HierarchyConfig config{
            /*l1=*/CacheConfig{64, 2, write_policy_selection.write_policy,
                               write_policy_selection.write_miss_policy, ReplacementPolicyType::LRU},
            /*l2=*/CacheConfig{128, 2, write_policy_selection.write_policy,
                               write_policy_selection.write_miss_policy, ReplacementPolicyType::LRU},
            /*l3=*/CacheConfig{256, 4, write_policy_selection.write_policy,
                               write_policy_selection.write_miss_policy, ReplacementPolicyType::LRU},
            /*inclusion_policy=*/inclusion_policy};

        CacheHierarchy<kL1BlockSizeBytes> hierarchy(config);

        if (argc > 1) {
            TraceRunner<CacheHierarchy<kL1BlockSizeBytes>> trace_runner(hierarchy);
            const TraceResult result = trace_runner.run_file(argv[1], std::cout);

            std::cout << "\noperations=" << result.operations
                      << " loads=" << result.load_operations
                      << " stores=" << result.store_operations << "\n";
            std::cout << "L1 read_hits=" << hierarchy.l1().read_hits()
                      << " read_misses=" << hierarchy.l1().read_misses()
                      << " write_hits=" << hierarchy.l1().write_hits()
                      << " write_misses=" << hierarchy.l1().write_misses()
                      << " writebacks=" << hierarchy.l1().writebacks()
                      << " compulsory_misses=" << hierarchy.l1().compulsory_misses()
                      << " capacity_misses=" << hierarchy.l1().capacity_misses()
                      << " conflict_misses=" << hierarchy.l1().conflict_misses() << "\n";
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

        std::cout << "\nL1 read_hits=" << hierarchy.l1().read_hits()
                  << " read_misses=" << hierarchy.l1().read_misses()
                  << " write_hits=" << hierarchy.l1().write_hits()
                  << " write_misses=" << hierarchy.l1().write_misses()
                  << " writebacks=" << hierarchy.l1().writebacks()
                  << " compulsory_misses=" << hierarchy.l1().compulsory_misses()
                  << " capacity_misses=" << hierarchy.l1().capacity_misses()
                  << " conflict_misses=" << hierarchy.l1().conflict_misses() << "\n";
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
