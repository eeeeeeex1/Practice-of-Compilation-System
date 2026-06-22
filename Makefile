# ToyC Compiler Makefile
# Compatible with MinGW-w64 on Windows

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic
LDFLAGS = 
TARGET = build/toyc.exe

# Source files
SRCS = src/main.cpp src/Lexer.cpp src/Parser.cpp src/SemanticAnalyzer.cpp src/CodeGenerator.cpp
OBJS = $(SRCS:.cpp=.o)

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS)

# Compile
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Clean
clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf build

# Rebuild
rebuild: clean all

# Test (requires RISC-V toolchain for full testing)
test: $(TARGET)
	@echo "Running basic compilation tests..."
	@for f in test/*.tc; do \
		echo "Testing $$f..."; \
		$(TARGET) < $$f > build/$$f.s 2>&1 || echo "FAILED: $$f"; \
	done

# Debug build
debug: CXXFLAGS += -g -O0
debug: rebuild

# Release build
release: CXXFLAGS += -O2
release: rebuild

.PHONY: all clean rebuild test debug release