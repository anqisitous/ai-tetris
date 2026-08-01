# ===================================================================
# Makefile for AI Tetris Project
# ===================================================================

# Configuration
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I.
LDFLAGS :=
BUILD_DIR := build
BIN_DIR := bin

# Targets
MAIN_TARGET := ai-tetris
TEST_TARGET := ai-tetris-tests

# Source files
MAIN_SOURCES := main_sdl3.cpp game_engine.cpp ai_core.cpp ai_evaluate.cpp ai_templates.cpp
TEST_SOURCES := tests/test_main.cpp tests/unit/test_game_engine.cpp tests/unit/test_ai_evaluate.cpp tests/unit/test_ai_core.cpp tests/integration/test_ai_decision.cpp tests/fixtures/test_fixtures.cpp

# SDL flags (for main executable)
SDL_CFLAGS := $(shell pkg-config --cflags sdl3 SDL3_ttf)
SDL_LDFLAGS := $(shell pkg-config --libs sdl3 SDL3_ttf)

.PHONY: all clean test run run-tests

all: $(MAIN_TARGET) $(TEST_TARGET)

# Main executable
$(MAIN_TARGET): $(MAIN_SOURCES)
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $^ -o $(BIN_DIR)/$@ $(SDL_LDFLAGS)

# Test executable
$(TEST_TARGET): $(TEST_SOURCES)
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $(BIN_DIR)/$@

# Build everything
build: all

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	rm -f *.o *~ core

# Run the main game
run: $(MAIN_TARGET)
	./$(BIN_DIR)/$(MAIN_TARGET)

# Run tests
test: $(TEST_TARGET)
	./$(BIN_DIR)/$(TEST_TARGET)

run-tests: test

# Debug build
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: all

# Release build
release: CXXFLAGS += -O3 -DNDEBUG
release: all

# Show help
help:
	@echo "AI Tetris Project Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make all          - Build main game and tests"
	@echo "  make build        - Same as all"
	@echo "  make run          - Build and run the game"
	@echo "  make test         - Build and run tests"
	@echo "  make run-tests    - Same as test"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make debug        - Build with debug symbols"
	@echo "  make release      - Build with optimizations"
	@echo "  make help         - Show this help message"