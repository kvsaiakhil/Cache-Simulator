#ifndef CACHE_SET_HPP
#define CACHE_SET_HPP

#include "cache_block.hpp"
#include "replacement_policy.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

template <uint32_t BlockSizeBytes>
class CacheSet {
public:
    explicit CacheSet(uint32_t ways, std::unique_ptr<ReplacementPolicy> replacement_policy)
        : blocks_(), replacement_policy_(std::move(replacement_policy)) {
        if (!replacement_policy_) {
            throw std::invalid_argument("replacement_policy must not be null");
        }

        blocks_.reserve(ways);
        for (uint32_t way = 0; way < ways; ++way) {
            blocks_.push_back(std::make_unique<CacheBlock<BlockSizeBytes>>());
        }
        replacement_policy_->reset(ways);
    }

    uint32_t ways() const {
        return static_cast<uint32_t>(blocks_.size());
    }

    int find_way_by_tag(uint32_t tag) const {
        for (uint32_t way = 0; way < blocks_.size(); ++way) {
            if (blocks_[way]->valid() && blocks_[way]->tag() == tag) {
                return static_cast<int>(way);
            }
        }
        return -1;
    }

    CacheBlockBase& block_at(uint32_t way) {
        return *blocks_.at(way);
    }

    const CacheBlockBase& block_at(uint32_t way) const {
        return *blocks_.at(way);
    }

    void record_access(uint32_t way, uint64_t access_counter) {
        replacement_policy_->on_access(way, access_counter);
    }

    uint32_t choose_victim_way() const {
        std::vector<bool> valid_bits;
        valid_bits.reserve(blocks_.size());
        for (const auto& block : blocks_) {
            valid_bits.push_back(block->valid());
        }
        return replacement_policy_->choose_victim(valid_bits);
    }

    CacheBlockBase& install_block_at_way(uint32_t victim_way,
                                         uint32_t tag,
                                         uint64_t access_counter) {
        CacheBlockBase& victim = *blocks_.at(victim_way);

        victim.reset();
        victim.set_tag(tag);
        victim.set_valid(true);
        victim.set_dirty(false);
        replacement_policy_->on_insert(victim_way, access_counter);
        return victim;
    }

private:
    std::vector<std::unique_ptr<CacheBlockBase>> blocks_;
    std::unique_ptr<ReplacementPolicy> replacement_policy_;
};

#endif
