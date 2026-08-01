cmake_minimum_required(VERSION 3.10)
project(AI-Tetris LANGUAGES CXX)

# Project version
set(PROJECT_VERSION "1.0.0")

# C++ standard (C++17 for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages (optional for tests)
find_package(SDL3 QUIET)
find_package(SDL3_ttf QUIET)

# Main executable
add_executable(ai-tetris 
    main_sdl3.cpp
    game_engine.cpp
    ai_core.cpp
    ai_evaluate.cpp
    ai_templates.cpp
)

if(SDL3_FOUND AND SDL3_ttf_FOUND)
    target_link_libraries(ai-tetris PRIVATE SDL3::SDL3 SDL3::SDL3_ttf)
endif()

# Include directories
target_include_directories(ai-tetris PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Enable testing
enable_testing()

# Add test executable
add_executable(ai-tetris-tests
    tests/simple_test_runner.cpp
    tests/fixtures/test_fixtures.cpp
    tests/fixtures/test_fixtures.h
    tests/test_config.h
    game_engine.cpp
    ai_evaluate.cpp
    ai_core.cpp
    ai_templates.cpp
)

# Link test executable with main code (excluding SDL for unit tests)
target_link_libraries(ai-tetris-tests PRIVATE)

# Include directories for tests
target_include_directories(ai-tetris-tests PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    tests
)

# Add Google Test if available (optional)
option(WITH_GTEST "Enable Google Test framework" OFF)
if(WITH_GTEST)
    find_package(GTest REQUIRED)
    target_link_libraries(ai-tetris-tests PRIVATE GTest::GTest GTest::Main)
    add_definitions(-DWITH_GTEST)
endif()

# Add tests to CTest
add_test(NAME ai-tetris-tests COMMAND ai-tetris-tests)

# Compile options
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -DDEBUG)
    target_compile_definitions(ai-tetris PRIVATE DEBUG)
    target_compile_definitions(ai-tetris-tests PRIVATE DEBUG)
else()
    add_compile_options(-O2 -DNDEBUG)
endif()

# Install targets (optional)
install(TARGETS ai-tetris DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION include)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

message(STATUS "AI-Tetris project configured successfully")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")