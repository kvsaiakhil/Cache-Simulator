#include "replacement_policy.hpp"

#include <limits>
#include <stdexcept>

void LruReplacementPolicy::reset(uint32_t ways) {
    last_access_.assign(ways, 0);
}

uint32_t LruReplacementPolicy::choose_victim(const std::vector<bool>& valid_bits) const {
    if (valid_bits.size() != last_access_.size()) {
        throw std::logic_error("Replacement policy state does not match set size");
    }

    for (uint32_t way = 0; way < valid_bits.size(); ++way) {
        if (!valid_bits[way]) {
            return way;
        }
    }

    uint32_t victim_way = 0;
    uint64_t oldest = std::numeric_limits<uint64_t>::max();
    for (uint32_t way = 0; way < last_access_.size(); ++way) {
        if (last_access_[way] < oldest) {
            oldest = last_access_[way];
            victim_way = way;
        }
    }
    return victim_way;
}

void LruReplacementPolicy::on_access(uint32_t way, uint64_t access_counter) {
    if (way >= last_access_.size()) {
        throw std::out_of_range("Replacement policy access way out of range");
    }
    last_access_[way] = access_counter;
}

void LruReplacementPolicy::on_insert(uint32_t way, uint64_t access_counter) {
    on_access(way, access_counter);
}

void FifoReplacementPolicy::reset(uint32_t ways) {
    insertion_order_.assign(ways, 0);
}

uint32_t FifoReplacementPolicy::choose_victim(const std::vector<bool>& valid_bits) const {
    if (valid_bits.size() != insertion_order_.size()) {
        throw std::logic_error("Replacement policy state does not match set size");
    }

    for (uint32_t way = 0; way < valid_bits.size(); ++way) {
        if (!valid_bits[way]) {
            return way;
        }
    }

    uint32_t victim_way = 0;
    uint64_t oldest = std::numeric_limits<uint64_t>::max();
    for (uint32_t way = 0; way < insertion_order_.size(); ++way) {
        if (insertion_order_[way] < oldest) {
            oldest = insertion_order_[way];
            victim_way = way;
        }
    }
    return victim_way;
}

void FifoReplacementPolicy::on_access(uint32_t, uint64_t) {
}

void FifoReplacementPolicy::on_insert(uint32_t way, uint64_t access_counter) {
    if (way >= insertion_order_.size()) {
        throw std::out_of_range("Replacement policy insert way out of range");
    }
    insertion_order_[way] = access_counter;
}

RandomReplacementPolicy::RandomReplacementPolicy(uint32_t seed)
    : state_(seed), ways_(0) {
}

void RandomReplacementPolicy::reset(uint32_t ways) {
    ways_ = ways;
}

uint32_t RandomReplacementPolicy::choose_victim(const std::vector<bool>& valid_bits) const {
    if (valid_bits.size() != ways_) {
        throw std::logic_error("Replacement policy state does not match set size");
    }

    for (uint32_t way = 0; way < valid_bits.size(); ++way) {
        if (!valid_bits[way]) {
            return way;
        }
    }

    return next_random() % ways_;
}

void RandomReplacementPolicy::on_access(uint32_t, uint64_t) {
}

void RandomReplacementPolicy::on_insert(uint32_t, uint64_t) {
}

uint32_t RandomReplacementPolicy::next_random() const {
    state_ = state_ * 1664525u + 1013904223u;
    return state_;
}

std::unique_ptr<ReplacementPolicy> create_replacement_policy(ReplacementPolicyType type) {
    switch (type) {
        case ReplacementPolicyType::LRU:
            return std::make_unique<LruReplacementPolicy>();
        case ReplacementPolicyType::FIFO:
            return std::make_unique<FifoReplacementPolicy>();
        case ReplacementPolicyType::Random:
            return std::make_unique<RandomReplacementPolicy>();
    }

    throw std::invalid_argument("Unsupported replacement policy");
}
