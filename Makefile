# Rolang compiler interface
CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
SEMANTIC_SOURCES := $(wildcard src/semantic/*.cpp)
SEMANTIC_HEADERS := $(wildcard src/semantic/*.hpp)
CPP_SOURCES := src/main.cpp src/frontend.cpp src/backend.cpp $(SEMANTIC_SOURCES)
CPP_OBJECTS := $(patsubst %.cpp,build/%.o,$(CPP_SOURCES))
TARGET := build/rolang
SEMANTIC_TEST_TARGET := build/semantic_tests
RUNTIME_TEST_TARGET := build/runtime_tests
SEMANTIC_TEST_OBJECTS := \
	build/tests/semantic_tests.o \
	build/src/frontend.o \
	$(patsubst src/semantic/%.cpp,build/src/semantic/%.o,$(SEMANTIC_SOURCES))
RUNTIME_TEST_OBJECTS := build/tests/runtime_tests.o build/src/frontend.o build/src/backend.o

.PHONY: all clean test
all: $(TARGET)

$(TARGET): $(CPP_OBJECTS)
	$(CXX) $(CXXFLAGS) -Isrc $(CPP_OBJECTS) -o $@

$(SEMANTIC_TEST_TARGET): $(SEMANTIC_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) -Isrc $(SEMANTIC_TEST_OBJECTS) -o $@

$(RUNTIME_TEST_TARGET): $(RUNTIME_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) -Isrc $(RUNTIME_TEST_OBJECTS) -o $@

build/%.o: %.cpp src/frontend.hpp src/backend.hpp $(SEMANTIC_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

clean:
	rm -rf build

test: $(TARGET) $(SEMANTIC_TEST_TARGET) $(RUNTIME_TEST_TARGET)
	sh ./tests/smoke.sh ./$(TARGET)
	python3 tests/test_cpp.py ./$(TARGET)
	./$(SEMANTIC_TEST_TARGET)
	./$(RUNTIME_TEST_TARGET)
