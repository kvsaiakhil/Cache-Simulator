#ifndef CACHE_STATS_HPP
#define CACHE_STATS_HPP

#include <cstdint>
#include <sstream>
#include <string>

class CacheStats {
public:
    CacheStats()
        : read_hits_(0),
          read_misses_(0),
          write_hits_(0),
          write_misses_(0),
          writebacks_(0),
          compulsory_misses_(0),
          capacity_misses_(0),
          conflict_misses_(0) {}

    void record_read_hit() { ++read_hits_; }
    void record_read_miss() { ++read_misses_; }
    void record_write_hit() { ++write_hits_; }
    void record_write_miss() { ++write_misses_; }
    void record_writeback() { ++writebacks_; }
    void record_compulsory_miss() { ++compulsory_misses_; }
    void record_capacity_miss() { ++capacity_misses_; }
    void record_conflict_miss() { ++conflict_misses_; }

    uint64_t read_hits() const { return read_hits_; }
    uint64_t read_misses() const { return read_misses_; }
    uint64_t write_hits() const { return write_hits_; }
    uint64_t write_misses() const { return write_misses_; }
    uint64_t writebacks() const { return writebacks_; }
    uint64_t compulsory_misses() const { return compulsory_misses_; }
    uint64_t capacity_misses() const { return capacity_misses_; }
    uint64_t conflict_misses() const { return conflict_misses_; }
    uint64_t total_misses() const {
        return read_misses_ + write_misses_;
    }

    static std::string csv_header() {
        return "level,read_hits,read_misses,write_hits,write_misses,writebacks,"
               "compulsory_misses,capacity_misses,conflict_misses";
    }

    std::string to_csv_row(const std::string& level_name) const {
        std::ostringstream os;
        os << level_name << ","
           << read_hits_ << ","
           << read_misses_ << ","
           << write_hits_ << ","
           << write_misses_ << ","
           << writebacks_ << ","
           << compulsory_misses_ << ","
           << capacity_misses_ << ","
           << conflict_misses_;
        return os.str();
    }

    std::string to_json(const std::string& level_name) const {
        std::ostringstream os;
        os << "\"" << level_name << "\":{"
           << "\"read_hits\":" << read_hits_ << ","
           << "\"read_misses\":" << read_misses_ << ","
           << "\"write_hits\":" << write_hits_ << ","
           << "\"write_misses\":" << write_misses_ << ","
           << "\"writebacks\":" << writebacks_ << ","
           << "\"compulsory_misses\":" << compulsory_misses_ << ","
           << "\"capacity_misses\":" << capacity_misses_ << ","
           << "\"conflict_misses\":" << conflict_misses_
           << "}";
        return os.str();
    }

private:
    uint64_t read_hits_;
    uint64_t read_misses_;
    uint64_t write_hits_;
    uint64_t write_misses_;
    uint64_t writebacks_;
    uint64_t compulsory_misses_;
    uint64_t capacity_misses_;
    uint64_t conflict_misses_;
};

#endif
