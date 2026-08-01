// ===================================================================
// test_fixtures.h - Test fixtures and utilities header
// ===================================================================

#pragma once

#include "../test_config.h"
#include "../../game_engine.h"
#include "../../ai_evaluate.h"

#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <chrono>

namespace TestFixtures {

// Board Fixtures
BoardBits createEmptyBoard();
BoardBits createSingleLineBoard();
BoardBits createWellBoard();
BoardBits createPerfectClearSetup();
BoardBits createBoardWithHoles();

// Player State Fixtures
PlayerState createBasicPlayerState(int seed = 42);
PlayerState createPlayerWithPiece(PType pieceType, int x = 3, int y = 0, int rot = 0);
PlayerState createPlayerWithBoard(const BoardBits& board, PType pieceType = PType::I);

// Test Utilities
bool boardsEqual(const BoardBits& a, const BoardBits& b);
int countFilledCells(const BoardBits& board);
int countFilledCellsInRow(const BoardBits& board, int row);
bool isRowFilled(const BoardBits& board, int row);
bool isRowEmpty(const BoardBits& board, int row);
BoardBits createBoardWithFilledRows(const std::vector<int>& filledRows);

// Assertion Helpers
void assertTrue(bool condition, const std::string& message = "");
void assertFalse(bool condition, const std::string& message = "");
void assertEqual(int expected, int actual, const std::string& message = "");
void assertEqual(float expected, float actual, float epsilon = 0.001f, const std::string& message = "");
void assertBoardsEqual(const BoardBits& expected, const BoardBits& actual, const std::string& message = "");

// Performance Testing
class PerformanceTimer {
private:
    std::chrono::high_resolution_clock::time_point start;
    std::string testName;
    
public:
    PerformanceTimer(const std::string& name);
    ~PerformanceTimer();
};

} // namespace TestFixtures