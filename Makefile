CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -Iinclude

BUILD_DIR := build
APP := $(BUILD_DIR)/cache_sim
TEST_BIN := $(BUILD_DIR)/cache_tests

.PHONY: all test clean

all: $(APP)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(APP): src/main.cpp src/replacement_policy.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/main.cpp src/replacement_policy.cpp -o $(APP)

$(TEST_BIN): tests/test_cache.cpp src/replacement_policy.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tests/test_cache.cpp src/replacement_policy.cpp -o $(TEST_BIN)

test: $(TEST_BIN)
	$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)
