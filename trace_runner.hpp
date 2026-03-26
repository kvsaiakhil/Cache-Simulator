#ifndef TRACE_RUNNER_HPP
#define TRACE_RUNNER_HPP

#include <cstdint>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

struct TraceResult {
    uint64_t operations;
    uint64_t load_operations;
    uint64_t store_operations;
};

template <typename CacheType>
class TraceRunner {
public:
    explicit TraceRunner(CacheType& cache) : cache_(cache) {}

    TraceResult run_file(const std::string& trace_path, std::ostream& os) {
        std::ifstream input(trace_path);
        if (!input) {
            throw std::runtime_error("Failed to open trace file: " + trace_path);
        }

        TraceResult result{0, 0, 0};
        std::string line;
        uint32_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            const std::string normalized = strip_comments_and_trim(line);
            if (normalized.empty()) {
                continue;
            }

            std::istringstream iss(normalized);
            char operation = '\0';
            std::string address_token;
            iss >> operation >> address_token;
            if (!iss || (operation != 'R' && operation != 'W')) {
                throw std::runtime_error("Invalid trace line " + std::to_string(line_number));
            }

            const uint32_t address = parse_u32(address_token);
            if (operation == 'R') {
                uint32_t value = 0;
                const bool hit = cache_.load(address, value);
                os << "R 0x" << std::hex << address << std::dec
                   << " -> " << value
                   << (hit ? " (hit)\n" : " (miss)\n");
                ++result.load_operations;
            } else {
                std::string value_token;
                iss >> value_token;
                if (!iss) {
                    throw std::runtime_error("Missing store value on trace line " +
                                             std::to_string(line_number));
                }
                validate_no_extra_tokens(iss, line_number);

                const uint32_t value = parse_u32(value_token);
                const bool hit = cache_.store(address, value);
                os << "W 0x" << std::hex << address << std::dec
                   << " <- " << value
                   << (hit ? " (hit)\n" : " (miss)\n");
                ++result.store_operations;
                ++result.operations;
                continue;
            }

            validate_no_extra_tokens(iss, line_number);
            ++result.operations;
        }

        return result;
    }

private:
    static std::string strip_comments_and_trim(const std::string& line) {
        const std::size_t comment_pos = line.find('#');
        const std::string without_comment =
            comment_pos == std::string::npos ? line : line.substr(0, comment_pos);

        std::size_t start = 0;
        while (start < without_comment.size() &&
               std::isspace(static_cast<unsigned char>(without_comment[start])) != 0) {
            ++start;
        }

        std::size_t end = without_comment.size();
        while (end > start &&
               std::isspace(static_cast<unsigned char>(without_comment[end - 1])) != 0) {
            --end;
        }

        return without_comment.substr(start, end - start);
    }

    static void validate_no_extra_tokens(std::istringstream& iss, uint32_t line_number) {
        std::string extra_token;
        if (iss >> extra_token) {
            throw std::runtime_error("Unexpected extra token on trace line " +
                                     std::to_string(line_number) + ": " + extra_token);
        }
    }

    static uint32_t parse_u32(const std::string& token) {
        if (!token.empty() && token[0] == '-') {
            throw std::runtime_error("Negative numeric token is not allowed: " + token);
        }

        std::size_t parsed_chars = 0;
        const unsigned long value = std::stoul(token, &parsed_chars, 0);
        if (parsed_chars != token.size()) {
            throw std::runtime_error("Invalid numeric token: " + token);
        }
        if (value > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Numeric token exceeds 32-bit range: " + token);
        }
        return static_cast<uint32_t>(value);
    }

    CacheType& cache_;
};

#endif
