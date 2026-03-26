#ifndef REPLACEMENT_POLICY_HPP
#define REPLACEMENT_POLICY_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include "cache_config.hpp"

class ReplacementPolicy {
public:
    virtual ~ReplacementPolicy() = default;

    virtual void reset(uint32_t ways) = 0;
    virtual uint32_t choose_victim(const std::vector<bool>& valid_bits) const = 0;
    virtual void on_access(uint32_t way, uint64_t access_counter) = 0;
    virtual void on_insert(uint32_t way, uint64_t access_counter) = 0;
};

class LruReplacementPolicy : public ReplacementPolicy {
public:
    LruReplacementPolicy() = default;

    void reset(uint32_t ways) override;
    uint32_t choose_victim(const std::vector<bool>& valid_bits) const override;
    void on_access(uint32_t way, uint64_t access_counter) override;
    void on_insert(uint32_t way, uint64_t access_counter) override;

private:
    std::vector<uint64_t> last_access_;
};

class FifoReplacementPolicy : public ReplacementPolicy {
public:
    FifoReplacementPolicy() = default;

    void reset(uint32_t ways) override;
    uint32_t choose_victim(const std::vector<bool>& valid_bits) const override;
    void on_access(uint32_t way, uint64_t access_counter) override;
    void on_insert(uint32_t way, uint64_t access_counter) override;

private:
    std::vector<uint64_t> insertion_order_;
};

class RandomReplacementPolicy : public ReplacementPolicy {
public:
    explicit RandomReplacementPolicy(uint32_t seed = 0xC0FFEEu);

    void reset(uint32_t ways) override;
    uint32_t choose_victim(const std::vector<bool>& valid_bits) const override;
    void on_access(uint32_t way, uint64_t access_counter) override;
    void on_insert(uint32_t way, uint64_t access_counter) override;

private:
    uint32_t next_random() const;

    mutable uint32_t state_;
    uint32_t ways_;
};

std::unique_ptr<ReplacementPolicy> create_replacement_policy(ReplacementPolicyType type);

#endif
