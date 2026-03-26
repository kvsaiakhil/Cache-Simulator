CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -Iinclude

BUILD_DIR := build
APP := $(BUILD_DIR)/cache_sim
TEST_BIN := $(BUILD_DIR)/cache_tests
FUZZ_BIN := $(BUILD_DIR)/cache_fuzz
COVERAGE_DIR := $(BUILD_DIR)/coverage
COVERAGE_REPORT_DIR := $(COVERAGE_DIR)/report
COVERAGE_TEST_BIN := $(COVERAGE_DIR)/cache_tests_coverage
COVERAGE_FUZZ_BIN := $(COVERAGE_DIR)/cache_fuzz_coverage
COVERAGE_FLAGS := --coverage -O0 -fno-inline
COVERAGE_FUZZ_ARGS ?= 16 128

.PHONY: all test fuzz coverage clean

all: $(APP)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(COVERAGE_DIR):
	mkdir -p $(COVERAGE_DIR)

$(COVERAGE_REPORT_DIR):
	mkdir -p $(COVERAGE_REPORT_DIR)

$(APP): src/main.cpp src/replacement_policy.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/main.cpp src/replacement_policy.cpp -o $(APP)

$(TEST_BIN): tests/test_cache.cpp src/replacement_policy.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tests/test_cache.cpp src/replacement_policy.cpp -o $(TEST_BIN)

test: $(TEST_BIN)
	$(TEST_BIN)

$(FUZZ_BIN): tests/fuzz_cache.cpp src/replacement_policy.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tests/fuzz_cache.cpp src/replacement_policy.cpp -o $(FUZZ_BIN)

fuzz: $(FUZZ_BIN)
	$(FUZZ_BIN)

$(COVERAGE_TEST_BIN): tests/test_cache.cpp src/replacement_policy.cpp | $(COVERAGE_DIR)
	$(CXX) $(CXXFLAGS) $(COVERAGE_FLAGS) tests/test_cache.cpp src/replacement_policy.cpp -o $(COVERAGE_TEST_BIN)

$(COVERAGE_FUZZ_BIN): tests/fuzz_cache.cpp src/replacement_policy.cpp | $(COVERAGE_DIR)
	$(CXX) $(CXXFLAGS) $(COVERAGE_FLAGS) tests/fuzz_cache.cpp src/replacement_policy.cpp -o $(COVERAGE_FUZZ_BIN)

coverage: $(COVERAGE_TEST_BIN) $(COVERAGE_FUZZ_BIN) | $(COVERAGE_REPORT_DIR)
	rm -f $(COVERAGE_DIR)/*.gcda $(COVERAGE_DIR)/*.gcov $(COVERAGE_REPORT_DIR)/*.gcov $(COVERAGE_REPORT_DIR)/coverage-summary.txt
	$(COVERAGE_TEST_BIN)
	$(COVERAGE_FUZZ_BIN) $(COVERAGE_FUZZ_ARGS)
	cd $(COVERAGE_REPORT_DIR) && gcov -b -c ../*.gcda 2>/dev/null | awk '\
		/^File '\''/ { keep = ($$0 ~ /'\''(tests\/|src\/|include\/cache_simulator\/)/); } \
		keep { print }' > coverage-summary.txt
	cat $(COVERAGE_REPORT_DIR)/coverage-summary.txt
	@echo
	@echo "Coverage artifacts written to $(COVERAGE_REPORT_DIR)"

clean:
	rm -rf $(BUILD_DIR)
