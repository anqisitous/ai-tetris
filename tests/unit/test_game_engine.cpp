// ===================================================================
// test_game_engine.cpp - Unit tests for game engine
// ===================================================================
#include "../../game_engine.h"
#include "../fixtures/test_fixtures.cpp"
#include <iostream>

namespace GameEngineTests {

// ===================================================================
// Collision Detection Tests
// ===================================================================

void testCollisionDetection_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    // Test with I piece at valid position
    bool collision = IsCollision(board, SHAPES[(int)PType::I][0], 3, 0);
    TestFixtures::assertFalse(collision, "I piece should not collide at (3,0) on empty board");
    
    // Test with I piece at invalid position (below board)
    collision = IsCollision(board, SHAPES[(int)PType::I][0], 3, -1);
    TestFixtures::assertTrue(collision, "I piece should collide below board");
    
    // Test with I piece at invalid position (left of board)
    collision = IsCollision(board, SHAPES[(int)PType::I][0], -1, 0);
    TestFixtures::assertTrue(collision, "I piece should collide left of board");
    
    // Test with I piece at invalid position (right of board)
    collision = IsCollision(board, SHAPES[(int)PType::I][0], 10, 0);
    TestFixtures::assertTrue(collision, "I piece should collide right of board");
}

void testCollisionDetection_WithExistingPieces() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    
    // Test with O piece above the filled line
    bool collision = IsCollision(board, SHAPES[(int)PType::O][0], 3, BOARD_H - 2);
    TestFixtures::assertFalse(collision, "O piece should not collide above filled line");
    
    // Test with O piece on the filled line
    collision = IsCollision(board, SHAPES[(int)PType::O][0], 3, BOARD_H - 1);
    TestFixtures::assertTrue(collision, "O piece should collide on filled line");
}

// ===================================================================
// Hard Drop Tests
// ===================================================================

void testHardDropY_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    // I piece should drop to bottom
    int dropY = HardDropY(board, SHAPES[(int)PType::I][0], 3);
    TestFixtures::assertEqual(BOARD_H - 4, dropY, "I piece should drop to y=16 (bottom - height)");
    
    // O piece should drop to bottom
    dropY = HardDropY(board, SHAPES[(int)PType::O][0], 3);
    TestFixtures::assertEqual(BOARD_H - 2, dropY, "O piece should drop to y=18 (bottom - height)");
}

void testHardDropY_WithObstacles() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    
    // I piece should drop to just above the filled line
    int dropY = HardDropY(board, SHAPES[(int)PType::I][0], 3);
    TestFixtures::assertEqual(BOARD_H - 5, dropY, "I piece should drop to just above filled line");
}

// ===================================================================
// Line Clearing Tests
// ===================================================================

void testClearLines_NoLines() {
    BoardBits board = TestFixtures::createEmptyBoard();
    int cleared = ClearLines(board);
    TestFixtures::assertEqual(0, cleared, "No lines should be cleared on empty board");
    TestFixtures::assertBoardsEqual(board, TestFixtures::createEmptyBoard(), "Board should remain unchanged");
}

void testClearLines_SingleLine() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    int cleared = ClearLines(board);
    TestFixtures::assertEqual(1, cleared, "One line should be cleared");
    TestFixtures::assertTrue(TestFixtures::isRowEmpty(board, BOARD_H - 1), "Bottom row should be empty after clearing");
}

void testClearLines_MultipleLines() {
    BoardBits board = TestFixtures::createBoardWithFilledRows({BOARD_H - 1, BOARD_H - 2, BOARD_H - 3});
    int cleared = ClearLines(board);
    TestFixtures::assertEqual(3, cleared, "Three lines should be cleared");
    
    // Check that the cleared lines are now empty
    for (int r = BOARD_H - 1; r >= BOARD_H - 3; --r) {
        TestFixtures::assertTrue(TestFixtures::isRowEmpty(board, r), "Row " + std::to_string(r) + " should be empty");
    }
}

void testClearLines_PartialLines() {
    BoardBits board = TestFixtures::createEmptyBoard();
    // Fill bottom row except one cell
    board[BOARD_H - 1] = 0x3FF ^ 0x01; // All except first column
    
    int cleared = ClearLines(board);
    TestFixtures::assertEqual(0, cleared, "No complete lines should be cleared");
}

// ===================================================================
// Damage Calculation Tests
// ===================================================================

void testCalculateDamage_BasicClears() {
    // Single line clear
    int damage = CalculateDamage(1, false, false, 0, false);
    TestFixtures::assertEqual(1, damage, "Single line should deal 1 damage");
    
    // Double line clear
    damage = CalculateDamage(2, false, false, 0, false);
    TestFixtures::assertEqual(2, damage, "Double line should deal 2 damage");
    
    // Triple line clear
    damage = CalculateDamage(3, false, false, 0, false);
    TestFixtures::assertEqual(3, damage, "Triple line should deal 3 damage");
    
    // Tetris (4 lines)
    damage = CalculateDamage(4, false, false, 0, false);
    TestFixtures::assertEqual(4, damage, "Tetris should deal 4 damage");
}

void testCalculateDamage_TSpins() {
    // T-Spin Mini
    int damage = CalculateDamage(0, true, false, 0, false);
    TestFixtures::assertEqual(1, damage, "T-Spin Mini should deal 1 damage");
    
    // T-Spin Single
    damage = CalculateDamage(1, true, false, 0, false);
    TestFixtures::assertEqual(2, damage, "T-Spin Single should deal 2 damage");
    
    // T-Spin Double
    damage = CalculateDamage(2, true, false, 0, false);
    TestFixtures::assertEqual(4, damage, "T-Spin Double should deal 4 damage");
    
    // T-Spin Triple
    damage = CalculateDamage(3, true, false, 0, false);
    TestFixtures::assertEqual(6, damage, "T-Spin Triple should deal 6 damage");
}

void testCalculateDamage_Combo() {
    // Combo multiplier
    int damage = CalculateDamage(1, false, false, 1, false);
    TestFixtures::assertEqual(1, damage, "First combo line should deal 1 damage");
    
    damage = CalculateDamage(1, false, false, 2, false);
    TestFixtures::assertEqual(2, damage, "Second combo line should deal 2 damage");
    
    damage = CalculateDamage(1, false, false, 5, false);
    TestFixtures::assertEqual(5, damage, "Fifth combo line should deal 5 damage");
}

void testCalculateDamage_BTB() {
    // Back-to-back bonus
    int damage = CalculateDamage(4, false, true, 0, false);
    TestFixtures::assertEqual(5, damage, "Tetris with BTB should deal 5 damage");
    
    damage = CalculateDamage(2, true, true, 0, false);
    TestFixtures::assertEqual(5, damage, "T-Spin Double with BTB should deal 5 damage");
}

void testCalculateDamage_PerfectClear() {
    // Perfect clear bonus
    int damage = CalculateDamage(4, false, false, 0, true);
    TestFixtures::assertEqual(10, damage, "Perfect clear Tetris should deal 10 damage");
}

// ===================================================================
// Player State Tests
// ===================================================================

void testPlayerState_Init() {
    PlayerState ps;
    ps.init(42);
    
    // Check initial state
    TestFixtures::assertFalse(ps.gameOver, "Game should not be over initially");
    TestFixtures::assertEqual(0, ps.score, "Score should be 0 initially");
    TestFixtures::assertEqual(1, ps.level, "Level should be 1 initially");
    TestFixtures::assertEqual(0, ps.linesCleared, "Lines cleared should be 0 initially");
    
    // Check that board is empty
    TestFixtures::assertEqual(0, TestFixtures::countFilledCells(ps.board), "Board should be empty initially");
}

void testPlayerState_PopNext() {
    PlayerState ps;
    ps.init(42);
    
    // Store initial next queue size
    size_t initialSize = ps.next.size();
    
    // Pop a piece
    PType piece = ps.popNext();
    
    // Check that next queue size decreased
    TestFixtures::assertEqual(initialSize - 1, ps.next.size(), "Next queue should have one less piece");
    
    // Check that current piece is set
    TestFixtures::assertTrue(ps.curType >= PType::I && ps.curType < PType::COUNT, "Current piece should be valid");
}

// ===================================================================
// Placement Enumeration Tests
// ===================================================================

void testEnumerateAllPlacements_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    auto placements = EnumerateAllPlacements(board, PType::I, true, PType::I, 0, 0);
    
    TestFixtures::assertTrue(!placements.empty(), "Should have at least one placement for I piece on empty board");
    
    // Check that all placements are valid
    for (const auto& placement : placements) {
        TestFixtures::assertTrue(placement.x >= 0 && placement.x < BOARD_W, "X position should be within bounds");
        TestFixtures::assertTrue(placement.y >= 0 && placement.y < BOARD_H, "Y position should be within bounds");
        TestFixtures::assertTrue(placement.rot >= 0 && placement.rot < 4, "Rotation should be valid");
    }
}

void testEnumerateAllPlacements_WithHold() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    // Test with hold available
    auto placementsWithHold = EnumerateAllPlacements(board, PType::I, true, PType::O, 0, 0);
    
    // Test without hold available
    auto placementsWithoutHold = EnumerateAllPlacements(board, PType::I, false, PType::O, 0, 0);
    
    // With hold should have more options (can choose to hold or not)
    TestFixtures::assertTrue(placementsWithHold.size() >= placementsWithoutHold.size(), 
                           "With hold should have equal or more placements");
}

// ===================================================================
// Garbage Addition Tests
// ===================================================================

void testAddGarbage_Basic() {
    BoardBits board = TestFixtures::createEmptyBoard();
    std::mt19937 rng(42);
    
    // Add garbage lines
    AddGarbage(board, 2, rng);
    
    // Check that garbage was added
    int filledCells = TestFixtures::countFilledCells(board);
    TestFixtures::assertTrue(filledCells > 0, "Garbage should add some filled cells");
    
    // Check that garbage was added at the bottom
    for (int r = 0; r < 2; ++r) {
        TestFixtures::assertTrue(TestFixtures::countFilledCellsInRow(board, r) > 0, 
                               "Garbage should be added at the bottom");
    }
}

// ===================================================================
// Run all game engine tests
// ===================================================================

void runAllTests() {
    std::cout << "Running Game Engine Tests..." << std::endl;
    
    // Collision Detection Tests
    testCollisionDetection_EmptyBoard();
    testCollisionDetection_WithExistingPieces();
    
    // Hard Drop Tests
    testHardDropY_EmptyBoard();
    testHardDropY_WithObstacles();
    
    // Line Clearing Tests
    testClearLines_NoLines();
    testClearLines_SingleLine();
    testClearLines_MultipleLines();
    testClearLines_PartialLines();
    
    // Damage Calculation Tests
    testCalculateDamage_BasicClears();
    testCalculateDamage_TSpins();
    testCalculateDamage_Combo();
    testCalculateDamage_BTB();
    testCalculateDamage_PerfectClear();
    
    // Player State Tests
    testPlayerState_Init();
    testPlayerState_PopNext();
    
    // Placement Enumeration Tests
    testEnumerateAllPlacements_EmptyBoard();
    testEnumerateAllPlacements_WithHold();
    
    // Garbage Addition Tests
    testAddGarbage_Basic();
    
    std::cout << "All Game Engine tests passed!" << std::endl;
}

} // namespace GameEngineTests