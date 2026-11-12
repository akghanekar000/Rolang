# Rolang compiler interface
CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
CPP_SOURCES := src/main.cpp src/frontend.cpp src/backend.cpp
TARGET := build/rolang

.PHONY: all clean test
all: $(TARGET)

$(TARGET): $(CPP_SOURCES) src/frontend.hpp src/backend.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $(CPP_SOURCES) -o $@

clean:
	rm -rf build

test: $(TARGET)
	./tests/smoke.sh ./$(TARGET)
