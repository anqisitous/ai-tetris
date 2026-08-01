// ===================================================================
// test_fixtures.cpp - Test fixtures and utilities implementation
// ===================================================================

#include "test_fixtures.h"

namespace TestFixtures {

// ===================================================================
// Board Fixtures
// ===================================================================

BoardBits createEmptyBoard() {
    BoardBits board = {};
    return board;
}

BoardBits createSingleLineBoard() {
    BoardBits board = {};
    board[BOARD_H - 1] = 0x3FF; // Fill bottom row completely
    return board;
}

BoardBits createWellBoard() {
    BoardBits board = {};
    // Create walls on both sides with a 4-cell well in the middle
    for (int r = BOARD_H - 4; r < BOARD_H; ++r) {
        board[r] = 0x3FF ^ 0x3C; // Fill everything except columns 2-5
    }
    return board;
}

BoardBits createPerfectClearSetup() {
    BoardBits board = {};
    // Create a flat surface at row 18
    board[BOARD_H - 2] = 0x3FF;
    return board;
}

BoardBits createBoardWithHoles() {
    BoardBits board = {};
    // Create some holes in the board
    for (int r = BOARD_H - 3; r < BOARD_H; ++r) {
        board[r] = 0x3FF ^ 0x55; // Alternating pattern with holes
    }
    return board;
}

// ===================================================================
// Player State Fixtures
// ===================================================================

PlayerState createBasicPlayerState(int seed) {
    PlayerState ps;
    ps.init(seed);
    return ps;
}

PlayerState createPlayerWithPiece(PType pieceType, int x, int y, int rot) {
    PlayerState ps;
    ps.init(42);
    ps.curType = pieceType;
    ps.curX = x;
    ps.curY = y;
    ps.curRot = rot;
    return ps;
}

PlayerState createPlayerWithBoard(const BoardBits& board, PType pieceType) {
    PlayerState ps;
    ps.init(42);
    ps.board = board;
    ps.curType = pieceType;
    ps.curX = 3;
    ps.curY = 0;
    ps.curRot = 0;
    return ps;
}

// ===================================================================
// Test Utilities
// ===================================================================

bool boardsEqual(const BoardBits& a, const BoardBits& b) {
    for (int i = 0; i < BOARD_BUFFER; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

int countFilledCells(const BoardBits& board) {
    int count = 0;
    for (int r = 0; r < BOARD_H; ++r) {
        count += __builtin_popcount(board[r]);
    }
    return count;
}

int countFilledCellsInRow(const BoardBits& board, int row) {
    if (row < 0 || row >= BOARD_BUFFER) return 0;
    return __builtin_popcount(board[row]);
}

bool isRowFilled(const BoardBits& board, int row) {
    if (row < 0 || row >= BOARD_BUFFER) return false;
    return board[row] == 0x3FF; // All 10 columns filled
}

bool isRowEmpty(const BoardBits& board, int row) {
    if (row < 0 || row >= BOARD_BUFFER) return true;
    return board[row] == 0;
}

BoardBits createBoardWithFilledRows(const std::vector<int>& filledRows) {
    BoardBits board = {};
    for (int row : filledRows) {
        if (row >= 0 && row < BOARD_BUFFER) {
            board[row] = 0x3FF;
        }
    }
    return board;
}

// ===================================================================
// Assertion Helpers
// ===================================================================

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message.empty() ? "Assertion failed" : message);
    }
}

void assertFalse(bool condition, const std::string& message) {
    assertTrue(!condition, message);
}

void assertEqual(int expected, int actual, const std::string& message) {
    if (expected != actual) {
        throw std::runtime_error(message.empty() ? 
            std::string("Expected ") + std::to_string(expected) + " but got " + std::to_string(actual) : message);
    }
}

void assertEqual(float expected, float actual, float epsilon, const std::string& message) {
    if (std::abs(expected - actual) > epsilon) {
        throw std::runtime_error(message.empty() ? 
            std::string("Expected ") + std::to_string(expected) + " but got " + std::to_string(actual) : message);
    }
}

void assertBoardsEqual(const BoardBits& expected, const BoardBits& actual, const std::string& message) {
    if (!boardsEqual(expected, actual)) {
        throw std::runtime_error(message.empty() ? "Boards are not equal" : message);
    }
}

// ===================================================================
// Performance Testing
// ===================================================================

PerformanceTimer::PerformanceTimer(const std::string& name) : testName(name) {
    start = std::chrono::high_resolution_clock::now();
}

PerformanceTimer::~PerformanceTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Performance: " << testName << " took " << elapsed.count() << " seconds" << std::endl;
}

} // namespace TestFixtures