CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -I.

APP := cache_sim
TEST_BIN := cache_tests

.PHONY: all test clean

all: $(APP)

$(APP): main.cpp replacement_policy.cpp
	$(CXX) $(CXXFLAGS) main.cpp replacement_policy.cpp -o $(APP)

$(TEST_BIN): tests/test_cache.cpp replacement_policy.cpp
	$(CXX) $(CXXFLAGS) tests/test_cache.cpp replacement_policy.cpp -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(APP) $(TEST_BIN)
