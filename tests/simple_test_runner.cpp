// ===================================================================
// simple_test_runner.cpp - Simple test runner for AI Tetris
// ===================================================================

// Include test config first
#include "test_config.h"

// Include the main headers
#include "../game_engine.h"
#include "../ai_evaluate.h"
#include "../ai_core.h"

// Include test fixtures header (not the .cpp file)
#include "fixtures/test_fixtures.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <chrono>

// Test counter
static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

void runTest(const std::string& name, void (*testFunc)()) {
    total_tests++;
    std::cout << "Running: " << name << " ... ";
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        testFunc();
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        
        passed_tests++;
        std::cout << "PASSED (" << elapsed.count() << "s)" << std::endl;
    } catch (const std::exception& e) {
        failed_tests++;
        std::cout << "FAILED: " << e.what() << std::endl;
    } catch (...) {
        failed_tests++;
        std::cout << "FAILED: Unknown exception" << std::endl;
    }
}

void printSummary() {
    std::cout << std::endl;
    std::cout << "=== Test Results ===" << std::endl;
    std::cout << "Total: " << total_tests << std::endl;
    std::cout << "Passed: " << passed_tests << std::endl;
    std::cout << "Failed: " << failed_tests << std::endl;
    
    if (failed_tests > 0) {
        std::exit(1);
    }
}

// ===================================================================
// Game Engine Tests
// ===================================================================

namespace GameEngineTests {

void testCollisionDetection_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    // Test with I piece at valid position
    bool collision = IsCollision(board, SHAPES[(int)PType::I][0], 3, 0);
    if (collision) throw std::runtime_error("I piece should not collide at (3,0) on empty board");
    
    // Test with I piece at invalid position (below board)
    collision = IsCollision(board, SHAPES[(int)PType::I][0], 3, -1);
    if (!collision) throw std::runtime_error("I piece should collide below board");
}

void testHardDropY_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    // I piece should drop to bottom
    int dropY = HardDropY(board, SHAPES[(int)PType::I][0], 3);
    if (dropY != BOARD_H - 4) {
        throw std::runtime_error("I piece should drop to y=16 (bottom - height)");
    }
}

void testClearLines_SingleLine() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    int cleared = ClearLines(board);
    if (cleared != 1) {
        throw std::runtime_error("One line should be cleared");
    }
}

void testCalculateDamage_Basic() {
    int damage = CalculateDamage(1, false, false, 0, false);
    if (damage != 1) {
        throw std::runtime_error("Single line should deal 1 damage");
    }
}

} // namespace GameEngineTests

// ===================================================================
// AI Evaluation Tests
// ===================================================================

namespace AIEvaluateTests {

void testExtractAspect_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    Aspect aspect = extractAspect(board);
    
    if (aspect.values.empty()) {
        throw std::runtime_error("Aspect should have values");
    }
}

void testCountHoles_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    int holes = countHoles(board);
    
    if (holes != 0) {
        throw std::runtime_error("Empty board should have 0 holes");
    }
}

void testEvaluateTerrainQuality() {
    BoardBits board = TestFixtures::createEmptyBoard();
    float quality = evaluateTerrainQuality(board);
    
    if (quality < 0) {
        throw std::runtime_error("Empty board should have non-negative quality");
    }
}

} // namespace AIEvaluateTests

// ===================================================================
// AI Core Tests
// ===================================================================

namespace AICoreTests {

void testAIState_Init() {
    AIState state;
    
    if (state.patternMemory.size() != 0) {
        throw std::runtime_error("Pattern memory should be empty initially");
    }
}

void testAIAction_DefaultValues() {
    AIAction action;
    
    if (action.targetX != 0) {
        throw std::runtime_error("Target X should be 0 by default");
    }
}

} // namespace AICoreTests

// ===================================================================
// Integration Tests
// ===================================================================

namespace IntegrationTests {

void testAI_Decision_Basic() {
    AIState aiState;
    PlayerState player1 = TestFixtures::createBasicPlayerState();
    PlayerState player2 = TestFixtures::createBasicPlayerState();
    
    AIAction action = decideAI(aiState, player1, player2);
    
    if (action.targetX < 0 || action.targetX >= BOARD_W) {
        throw std::runtime_error("AI should choose valid X position");
    }
}

} // namespace IntegrationTests

// ===================================================================
// Main test runner
// ===================================================================

int main(int argc, char* argv[]) {
    std::cout << "=== AI Tetris Test Suite ===" << std::endl;
    std::cout << "Running comprehensive tests..." << std::endl;
    std::cout << std::endl;
    
    // Game Engine Tests
    std::cout << "--- Game Engine Tests ---" << std::endl;
    runTest("Collision Detection - Empty Board", GameEngineTests::testCollisionDetection_EmptyBoard);
    runTest("Hard Drop - Empty Board", GameEngineTests::testHardDropY_EmptyBoard);
    runTest("Clear Lines - Single Line", GameEngineTests::testClearLines_SingleLine);
    runTest("Calculate Damage - Basic", GameEngineTests::testCalculateDamage_Basic);
    
    // AI Evaluation Tests
    std::cout << "\n--- AI Evaluation Tests ---" << std::endl;
    runTest("Extract Aspect - Empty Board", AIEvaluateTests::testExtractAspect_EmptyBoard);
    runTest("Count Holes - Empty Board", AIEvaluateTests::testCountHoles_EmptyBoard);
    runTest("Evaluate Terrain Quality", AIEvaluateTests::testEvaluateTerrainQuality);
    
    // AI Core Tests
    std::cout << "\n--- AI Core Tests ---" << std::endl;
    runTest("AI State - Init", AICoreTests::testAIState_Init);
    runTest("AI Action - Default Values", AICoreTests::testAIAction_DefaultValues);
    
    // Integration Tests
    std::cout << "\n--- Integration Tests ---" << std::endl;
    runTest("AI Decision - Basic", IntegrationTests::testAI_Decision_Basic);
    
    // Print summary
    printSummary();
    
    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}