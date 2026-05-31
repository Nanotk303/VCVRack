RACK_DIR ?= ../../Rack-SDK

FLAGS +=
CXXFLAGS += -std=c++17

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += plugin.json

.PHONY: test
test:
	mkdir -p build/tests
	$(CXX) -std=c++17 -Wall -Wextra -pedantic tests/test_mts_tuning.cpp -o build/tests/test_mts_tuning
	build/tests/test_mts_tuning

include $(RACK_DIR)/plugin.mk
