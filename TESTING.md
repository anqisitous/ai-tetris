# AI Tetris Testing Strategy

## Overview

This document outlines the comprehensive testing strategy for the AI Tetris project. The project includes a sophisticated Tetris game engine, AI decision-making system, advanced terrain evaluation, and template-based opening strategies.

## Test Architecture

```
tests/
├── unit/                    # Unit tests for individual components
│   ├── test_game_engine.cpp # Game engine functionality tests
│   ├── test_ai_evaluate.cpp # AI evaluation system tests
│   └── test_ai_core.cpp     # AI core functionality tests
├── integration/             # Integration tests for system interactions
│   └── test_ai_decision.cpp # AI decision-making integration tests
├── fixtures/                # Test utilities and fixtures
│   └── test_fixtures.cpp    # Common test fixtures and helpers
└── test_main.cpp            # Main test runner
```

## Test Categories

### 1. Unit Tests

#### Game Engine Tests (`test_game_engine.cpp`)
- **Collision Detection**: Tests for piece collision with board boundaries and existing pieces
- **Hard Drop**: Tests for calculating the lowest possible position for a piece
- **Line Clearing**: Tests for detecting and clearing completed lines
- **Damage Calculation**: Tests for calculating damage based on clears, T-spins, combos, and BTB
- **Player State**: Tests for player initialization and state management
- **Placement Enumeration**: Tests for generating all possible piece placements
- **Garbage Addition**: Tests for adding garbage lines to opponent's board

#### AI Evaluation Tests (`test_ai_evaluate.cpp`)
- **Aspect Extraction**: Tests for extracting board features for AI evaluation
- **Height Calculation**: Tests for calculating column heights and group heights
- **Hole Detection**: Tests for detecting and counting holes in the board
- **T-Spin Detection**: Tests for detecting T-spin setups and executions
- **Double Dagger Detection**: Tests for detecting double dagger formations
- **Terrain Quality**: Tests for evaluating overall board quality
- **Parity Calculations**: Tests for horizontal and vertical parity analysis
- **Perfect Clear Theorem**: Tests for perfect clear possibility evaluation
- **Spin Detection**: Tests for polymorphic spin detection system
- **Reachable Space Analysis**: Tests for BFS-based reachable space detection
- **Hole Evaluation**: Tests for evaluating individual holes
- **Tetris Well Evaluation**: Tests for evaluating Tetris well formations
- **Surface Evaluation**: Tests for evaluating board surface quality
- **Hole Filling**: Tests for evaluating hole-filling opportunities

#### AI Core Tests (`test_ai_core.cpp`)
- **Pattern Memory**: Tests for dynamic pattern learning and matching
- **AI State**: Tests for AI state initialization and management
- **AI Actions**: Tests for AI action creation and validation
- **Decision Making**: Tests for AI decision-making process
- **AI Execution**: Tests for executing AI actions on player states
- **Learning**: Tests for AI learning from gameplay
- **Template System**: Tests for template-based opening strategies

### 2. Integration Tests

#### AI Decision Integration Tests (`test_ai_decision.cpp`)
- **End-to-End AI Decisions**: Tests for complete AI decision-making workflow
- **AI vs AI Simulation**: Tests for multi-step AI vs AI gameplay
- **AI Learning Integration**: Tests for AI learning from actual gameplay
- **AI with Templates**: Tests for AI using template-based strategies
- **AI Evaluation Integration**: Tests for AI handling various board states
- **Performance Tests**: Tests for AI decision-making performance
- **Edge Cases**: Tests for handling edge cases like game over and full boards

## Test Execution

### Building Tests

```bash
# Create build directory
mkdir build
cd build

# Configure with CMake
cmake ..

# Build the test executable
cmake --build . --target ai-tetris-tests
```

### Running Tests

```bash
# Run all tests
./bin/ai-tetris-tests

# Or run with CTest
ctest --output-on-failure
```

### Running Specific Test Categories

The test runner currently runs all tests together. For more granular control:

```bash
# Run only game engine tests
# (This would require modifying the test runner or using test discovery)
```

## Test Coverage Goals

### Game Engine (Target: 95%+)
- [ ] Collision detection for all piece types and rotations
- [ ] Hard drop calculation for all piece types
- [ ] Line clearing for 1-4 lines
- [ ] Damage calculation for all combinations (clears, T-spins, combos, BTB, perfect clear)
- [ ] Player state management
- [ ] Piece placement enumeration
- [ ] Garbage line addition

### AI Evaluation (Target: 90%+)
- [ ] Aspect extraction for various board states
- [ ] Height calculations for all columns
- [ ] Hole detection and counting
- [ ] T-spin detection for all T-spin types
- [ ] Double dagger detection
- [ ] Terrain quality evaluation
- [ ] Parity calculations
- [ ] Perfect clear theorem validation
- [ ] Spin detection polymorphism
- [ ] Reachable space analysis
- [ ] Hole and well evaluation
- [ ] Surface evaluation
- [ ] Hole filling evaluation

### AI Core (Target: 85%+)
- [ ] Pattern memory operations
- [ ] AI state management
- [ ] Action creation and validation
- [ ] Decision-making process
- [ ] Action execution
- [ ] Learning from gameplay
- [ ] Template matching and usage

## Test Data and Fixtures

The test fixtures provide common board states and player configurations:

- **Empty Board**: Completely empty 10x20 board
- **Single Line Board**: Board with one completed line at the bottom
- **Well Board**: Board with a well (hole) in the middle
- **Perfect Clear Setup**: Board set up for potential perfect clear
- **Board with Holes**: Board with intentional holes for testing
- **Basic Player States**: Players with various configurations

## Performance Testing

### Performance Benchmarks
- AI decision time: < 100ms per decision (target)
- Placement enumeration: < 10ms for empty board (target)
- Terrain evaluation: < 5ms per evaluation (target)

### Performance Test Execution
```bash
# Run performance tests
./bin/ai-tetris-tests --performance
```

## Continuous Integration

### GitHub Actions Configuration

The project should include a `.github/workflows/test.yml` file for automated testing:

```yaml
name: AI Tetris Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v2
    
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y cmake build-essential libsdl3-dev libsdl3-ttf-dev
    
    - name: Configure
      run: cmake -B build
    
    - name: Build
      run: cmake --build build
    
    - name: Test
      run: ctest --output-on-failure -C build
```

## Test Reporting

### Test Output Format
Tests output results in the following format:

```
=== AI Tetris Test Suite ===
Running X tests...

Running: test_name ... PASSED (0.001s)
Running: test_name ... PASSED (0.002s)
...

=== Test Results ===
Total: X
Passed: Y
Failed: Z

Failed tests:
  - test_name: failure_message
```

### Test Coverage Reporting

For code coverage analysis, use:

```bash
# Install gcov and lcov
sudo apt-get install gcov lcov

# Build with coverage flags
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage" ..
cmake --build .

# Run tests to generate coverage data
ctest

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage
```

## Test Maintenance

### Adding New Tests
1. Create a new test file in the appropriate directory (`unit/`, `integration/`, etc.)
2. Include necessary headers
3. Use the test fixtures for common setups
4. Follow the existing test naming conventions
5. Add the test to the main test runner

### Test Naming Conventions
- Use descriptive names: `testComponent_Functionality_Scenario`
- Group related tests in namespaces
- Use camelCase for test function names
- Use PascalCase for test namespaces

### Test Best Practices
- Each test should test one specific functionality
- Tests should be independent of each other
- Use assertions to validate expected behavior
- Include meaningful error messages
- Test both normal and edge cases
- Keep tests fast and focused

## Known Limitations

1. **SDL Dependency**: Some tests may require SDL3 for graphics, but unit tests should avoid SDL dependencies
2. **Randomness**: Tests involving random number generation should use fixed seeds for reproducibility
3. **Performance**: Some AI functions may be computationally expensive to test exhaustively
4. **Template System**: Template matching tests may require specific board configurations

## Future Test Enhancements

1. **Property-Based Testing**: Add randomized testing for edge cases
2. **Fuzz Testing**: Add fuzz testing for input validation
3. **Benchmark Suite**: Add comprehensive performance benchmarks
4. **Visual Testing**: Add tests that can visualize board states for debugging
5. **Multiplayer Testing**: Add tests for multiplayer scenarios
6. **Replay Testing**: Add tests that can replay actual games for regression testing

## Test Environment Requirements

- C++17 compatible compiler
- CMake 3.10+
- SDL3 and SDL3_ttf libraries (for main game, not required for unit tests)
- Standard C++ library

## Troubleshooting

### Common Issues

1. **Missing Dependencies**: Ensure all required libraries are installed
2. **Compilation Errors**: Check C++ standard compatibility
3. **Test Failures**: Run tests with verbose output to see detailed failure messages
4. **Performance Issues**: Reduce test complexity or increase timeouts

### Debugging Tests

```bash
# Run tests with verbose output
ctest --verbose

# Run specific test with debugging
gdb --args ./bin/ai-tetris-tests
```

## Conclusion

This testing strategy provides comprehensive coverage of the AI Tetris project, ensuring that all components work correctly both in isolation and together. The modular test structure allows for easy maintenance and expansion as the project evolves.