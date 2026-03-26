#ifndef CACHE_BLOCK_HPP
#define CACHE_BLOCK_HPP

#include <array>
#include <cstdint>
#include <stdexcept>

class CacheBlockBase {
public:
    virtual ~CacheBlockBase() = default;

    virtual uint32_t tag() const = 0;
    virtual void set_tag(uint32_t tag) = 0;

    virtual bool valid() const = 0;
    virtual void set_valid(bool valid) = 0;

    virtual bool dirty() const = 0;
    virtual void set_dirty(bool dirty) = 0;

    virtual uint32_t block_size_bytes() const = 0;
    virtual uint8_t read_byte(uint32_t byte_offset) const = 0;
    virtual void write_byte(uint32_t byte_offset, uint8_t value) = 0;

    virtual uint32_t read_word(uint32_t byte_offset) const = 0;
    virtual void write_word(uint32_t byte_offset, uint32_t value) = 0;

    virtual void reset() = 0;
};

template <uint32_t BlockSizeBytes>
struct CacheLineSnapshot {
    std::array<uint8_t, BlockSizeBytes> data{};
    bool dirty = false;
};

template <uint32_t BlockSizeBytes>
class CacheBlock : public CacheBlockBase {
public:
    static constexpr uint32_t kWordSizeBytes = 4;

    static_assert(BlockSizeBytes > 0, "BlockSizeBytes must be non-zero");
    static_assert((BlockSizeBytes % kWordSizeBytes) == 0,
                  "BlockSizeBytes must be a multiple of 4");

    CacheBlock() : tag_(0), valid_(false), dirty_(false), data_{} {}

    uint32_t tag() const override {
        return tag_;
    }

    void set_tag(uint32_t tag) override {
        tag_ = tag;
    }

    bool valid() const override {
        return valid_;
    }

    void set_valid(bool valid) override {
        valid_ = valid;
    }

    bool dirty() const override {
        return dirty_;
    }

    void set_dirty(bool dirty) override {
        dirty_ = dirty;
    }

    uint32_t block_size_bytes() const override {
        return BlockSizeBytes;
    }

    uint8_t read_byte(uint32_t byte_offset) const override {
        if (byte_offset >= BlockSizeBytes) {
            throw std::out_of_range("Block byte offset out of range");
        }
        return data_[byte_offset];
    }

    void write_byte(uint32_t byte_offset, uint8_t value) override {
        if (byte_offset >= BlockSizeBytes) {
            throw std::out_of_range("Block byte offset out of range");
        }
        data_[byte_offset] = value;
    }

    uint32_t read_word(uint32_t byte_offset) const override {
        if ((byte_offset % kWordSizeBytes) != 0) {
            throw std::invalid_argument("Block word read must be 4-byte aligned");
        }
        if (byte_offset + kWordSizeBytes > BlockSizeBytes) {
            throw std::out_of_range("Block word read exceeds block size");
        }

        return static_cast<uint32_t>(data_[byte_offset]) |
               (static_cast<uint32_t>(data_[byte_offset + 1]) << 8) |
               (static_cast<uint32_t>(data_[byte_offset + 2]) << 16) |
               (static_cast<uint32_t>(data_[byte_offset + 3]) << 24);
    }

    void write_word(uint32_t byte_offset, uint32_t value) override {
        if ((byte_offset % kWordSizeBytes) != 0) {
            throw std::invalid_argument("Block word write must be 4-byte aligned");
        }
        if (byte_offset + kWordSizeBytes > BlockSizeBytes) {
            throw std::out_of_range("Block word write exceeds block size");
        }

        data_[byte_offset] = static_cast<uint8_t>(value & 0xFFu);
        data_[byte_offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        data_[byte_offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        data_[byte_offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    }

    void reset() override {
        tag_ = 0;
        valid_ = false;
        dirty_ = false;
        data_.fill(0);
    }

private:
    uint32_t tag_;
    bool valid_;
    bool dirty_;
    std::array<uint8_t, BlockSizeBytes> data_;
};

#endif
