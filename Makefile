CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -Iinclude

BUILD_DIR := build
APP := $(BUILD_DIR)/cache_sim
TEST_BIN := $(BUILD_DIR)/cache_tests
FUZZ_BIN := $(BUILD_DIR)/cache_fuzz

.PHONY: all test fuzz clean

all: $(APP)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

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

clean:
	rm -rf $(BUILD_DIR)
