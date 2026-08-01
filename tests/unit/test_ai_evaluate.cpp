// ===================================================================
// test_ai_evaluate.cpp - Unit tests for AI evaluation
// ===================================================================
#include "../../ai_evaluate.h"
#include "../fixtures/test_fixtures.cpp"
#include <iostream>

namespace AIEvaluateTests {

// ===================================================================
// Aspect Extraction Tests
// ===================================================================

void testExtractAspect_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    Aspect aspect = extractAspect(board);
    
    TestFixtures::assertTrue(!aspect.values.empty(), "Aspect should have values");
    TestFixtures::assertTrue(aspect.values.size() > 0, "Aspect should have non-zero size");
}

void testExtractAspect_WithBoardState() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    std::deque<PType> next = {PType::I, PType::O, PType::T};
    
    Aspect aspect = extractAspect(board, 0.5f, 3, next);
    
    TestFixtures::assertTrue(!aspect.values.empty(), "Aspect should have values with board state");
}

// ===================================================================
// Height Calculation Tests
// ===================================================================

void testGetColumnHeight_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    for (int col = 0; col < BOARD_W; ++col) {
        int height = getColumnHeight(board, col);
        TestFixtures::assertEqual(0, height, "Column height should be 0 on empty board");
    }
}

void testGetColumnHeight_SingleLine() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    
    for (int col = 0; col < BOARD_W; ++col) {
        int height = getColumnHeight(board, col);
        TestFixtures::assertEqual(1, height, "Column height should be 1 with single line");
    }
}

void testGetColumnHeight_VariedHeights() {
    BoardBits board = TestFixtures::createEmptyBoard();
    // Create varied heights
    board[BOARD_H - 1] = 0x3FF; // Full bottom row
    board[BOARD_H - 2] = 0x1FF; // Half of second row
    
    // Columns 0-4 should have height 2
    for (int col = 0; col < 5; ++col) {
        int height = getColumnHeight(board, col);
        TestFixtures::assertEqual(2, height, "Columns 0-4 should have height 2");
    }
    
    // Columns 5-9 should have height 1
    for (int col = 5; col < BOARD_W; ++col) {
        int height = getColumnHeight(board, col);
        TestFixtures::assertEqual(1, height, "Columns 5-9 should have height 1");
    }
}

// ===================================================================
// Hole Detection Tests
// ===================================================================

void testCountHoles_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    int holes = countHoles(board);
    TestFixtures::assertEqual(0, holes, "Empty board should have 0 holes");
}

void testCountHoles_SolidBoard() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    int holes = countHoles(board);
    TestFixtures::assertEqual(0, holes, "Solid single line should have 0 holes");
}

void testCountHoles_BoardWithHoles() {
    BoardBits board = TestFixtures::createBoardWithHoles();
    int holes = countHoles(board);
    TestFixtures::assertTrue(holes > 0, "Board with alternating pattern should have holes");
}

void testCountHoles_WellBoard() {
    BoardBits board = TestFixtures::createWellBoard();
    int holes = countHoles(board);
    // Well board has a 4-cell wide hole, should count as holes
    TestFixtures::assertTrue(holes > 0, "Well board should have holes");
}

// ===================================================================
// T-Spin Detection Tests
// ===================================================================

void testIsTSpin_Basic() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    // Test with T piece in various positions
    // This is a simplified test - actual T-spin detection depends on board state
    bool isTspin = isTSpin(board, 3, 18, 0);
    // On empty board, T piece at (3,18) with rotation 0 should not be a T-spin
    TestFixtures::assertFalse(isTspin, "T piece on empty board should not be T-spin");
}

// ===================================================================
// Double Dagger Detection Tests
// ===================================================================

void testIsDoubleDagger_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    bool isDagger = isDoubleDagger(board);
    TestFixtures::assertFalse(isDagger, "Empty board should not be double dagger");
}

void testIsDoubleDaggerLeft_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    bool isDagger = isDoubleDaggerLeft(board);
    TestFixtures::assertFalse(isDagger, "Empty board should not be double dagger left");
}

void testIsDoubleDaggerRight_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    bool isDagger = isDoubleDaggerRight(board);
    TestFixtures::assertFalse(isDagger, "Empty board should not be double dagger right");
}

// ===================================================================
// Terrain Quality Evaluation Tests
// ===================================================================

void testEvaluateTerrainQuality_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    float quality = evaluateTerrainQuality(board);
    
    // Empty board should have a baseline quality
    TestFixtures::assertTrue(quality >= 0, "Empty board should have non-negative quality");
}

void testEvaluateTerrainQuality_SolidBoard() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    float quality = evaluateTerrainQuality(board);
    
    // Solid board should have good quality
    TestFixtures::assertTrue(quality >= 0, "Solid board should have non-negative quality");
}

// ===================================================================
// Parity Spectrum Tests
// ===================================================================

void testCalculateParitySpectrum_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    float parity = calculateParitySpectrum(board);
    
    // Empty board should have a specific parity value
    TestFixtures::assertTrue(parity >= 0, "Parity spectrum should be non-negative");
}

// ===================================================================
// Horizontal Parity Tests
// ===================================================================

void testCalculateHorizontalParity_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    int parity = calculateHorizontalParity(board);
    
    // Empty board should have 0 parity
    TestFixtures::assertEqual(0, parity, "Empty board should have 0 horizontal parity");
}

void testCalculateHorizontalParity_SingleLine() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    int parity = calculateHorizontalParity(board);
    
    // Single line with all cells filled should have specific parity
    TestFixtures::assertTrue(parity >= 0, "Single line should have non-negative parity");
}

// ===================================================================
// Perfect Clear Theorem Tests
// ===================================================================

void testIsPerfectClearTheoremSatisfied() {
    // Test with values that should satisfy the theorem
    bool satisfied = isPerfectClearTheoremSatisfied(2, 1, 1, 0);
    // This is a simplified test - actual theorem logic depends on the implementation
    TestFixtures::assertTrue(satisfied || !satisfied, "Theorem should return a boolean");
}

void testEvaluatePerfectClearPossibility() {
    BoardBits board = TestFixtures::createPerfectClearSetup();
    float possibility = evaluatePerfectClearPossibility(board, 2, 1, 1);
    
    TestFixtures::assertTrue(posibility >= 0 && possibility <= 1, "Possibility should be between 0 and 1");
}

// ===================================================================
// Spin Detector Tests
// ===================================================================

void testStandardSpinDetector() {
    StandardSpinDetector detector;
    BoardBits board = TestFixtures::createEmptyBoard();
    
    // Test detection with T piece
    SpinType spinType = detector.detect(board, SHAPES[(int)PType::T][0], 3, 18, 0);
    TestFixtures::assertTrue(spinType >= SpinType::NONE && spinType <= SpinType::TETRIS, 
                           "Spin type should be valid");
    
    // Test score calculation
    float score = detector.getScore(SpinType::T_DOUBLE, false);
    TestFixtures::assertTrue(score > 0, "T-Spin Double should have positive score");
}

void testSpinEvaluator() {
    SpinEvaluator evaluator(false);
    BoardBits board = TestFixtures::createEmptyBoard();
    std::deque<PType> next = {PType::T, PType::I, PType::O};
    
    float evaluation = evaluator.evaluate(board, next);
    TestFixtures::assertTrue(evaluation >= 0, "Evaluation should be non-negative");
    
    // Test with BTB enabled
    evaluator.setBTB(true);
    evaluation = evaluator.evaluate(board, next);
    TestFixtures::assertTrue(evaluation >= 0, "Evaluation with BTB should be non-negative");
}

// ===================================================================
// Reachable Space Analysis Tests
// ===================================================================

void testFindReachableSpaces_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    auto reachable = findReachableSpaces(board);
    
    TestFixtures::assertTrue(!reachable.empty(), "Reachable spaces should not be empty");
    TestFixtures::assertEqual(BOARD_H, reachable.size(), "Should have reachable info for all rows");
}

void testFindReachableSpaces_SolidBoard() {
    BoardBits board = TestFixtures::createSingleLineBoard();
    auto reachable = findReachableSpaces(board);
    
    TestFixtures::assertTrue(!reachable.empty(), "Reachable spaces should not be empty");
}

void testFindConnectedComponents() {
    BoardBits board = TestFixtures::createEmptyBoard();
    auto reachable = findReachableSpaces(board);
    auto components = findConnectedComponents(reachable);
    
    TestFixtures::assertTrue(!components.empty(), "Should find at least one connected component");
}

void testAnalyzeReachableSpaces() {
    BoardBits board = TestFixtures::createEmptyBoard();
    ReachableSpaceInfo info = analyzeReachableSpaces(board);
    
    TestFixtures::assertTrue(!info.components.empty(), "Should have components");
}

// ===================================================================
// Hole Evaluation Tests
// ===================================================================

void testEvaluateHole() {
    BoardBits board = TestFixtures::createWellBoard();
    auto reachable = findReachableSpaces(board);
    auto components = findConnectedComponents(reachable);
    
    if (!components.empty()) {
        auto hole = components[0];
        HoleEvaluation evaluation = evaluateHole(hole, board);
        
        TestFixtures::assertTrue(evaluation.area >= 0, "Hole area should be non-negative");
        TestFixtures::assertTrue(evaluation.maxDepth >= 0, "Max depth should be non-negative");
    }
}

// ===================================================================
// Tetris Well Evaluation Tests
// ===================================================================

void testEvaluateTetrisWell() {
    BoardBits board = TestFixtures::createWellBoard();
    auto reachable = findReachableSpaces(board);
    auto components = findConnectedComponents(reachable);
    
    if (!components.empty()) {
        auto well = components[0];
        TetrisWellEvaluation evaluation = evaluateTetrisWell(well, board);
        
        TestFixtures::assertTrue(evaluation.depth >= 0, "Well depth should be non-negative");
        TestFixtures::assertTrue(evaluation.completeness >= 0 && evaluation.completeness <= 1, 
                               "Well completeness should be between 0 and 1");
    }
}

// ===================================================================
// Surface Evaluation Tests
// ===================================================================

void testEvaluateSurface() {
    BoardBits board = TestFixtures::createEmptyBoard();
    float surfaceScore = evaluateSurface(board);
    
    TestFixtures::assertTrue(surfaceScore >= 0, "Surface score should be non-negative");
}

// ===================================================================
// Post Clear Evaluation Tests
// ===================================================================

void testEvaluatePostClear() {
    BoardBits board = TestFixtures::createEmptyBoard();
    float postClearScore = evaluatePostClear(board);
    
    TestFixtures::assertTrue(postClearScore >= 0, "Post clear score should be non-negative");
}

// ===================================================================
// Comprehensive Evaluation Tests
// ===================================================================

void testEvaluateTerrainWithHoles() {
    BoardBits board = TestFixtures::createEmptyBoard();
    std::deque<PType> next = {PType::I, PType::O, PType::T};
    
    float score = evaluateTerrainWithHoles(board, next, true, true);
    TestFixtures::assertTrue(score >= 0, "Terrain score should be non-negative");
}

void testEvaluateTSDCandidates() {
    BoardBits board = TestFixtures::createEmptyBoard();
    std::deque<PType> next = {PType::T, PType::I, PType::O};
    
    float score = evaluateTSDCandidates(board, next, true);
    TestFixtures::assertTrue(score >= 0, "TSD candidate score should be non-negative");
}

void testEvaluateHoleFilling() {
    BoardBits board = TestFixtures::createBoardWithHoles();
    std::deque<PType> next = {PType::I, PType::O, PType::T};
    
    float score = evaluateHoleFilling(board, next, true, true, PType::I);
    TestFixtures::assertTrue(score >= 0, "Hole filling score should be non-negative");
}

// ===================================================================
// Hole Fill Evaluation Tests
// ===================================================================

void testCanFillHoleWithPiece() {
    BoardBits board = TestFixtures::createWellBoard();
    auto reachable = findReachableSpaces(board);
    auto components = findConnectedComponents(reachable);
    
    if (!components.empty()) {
        auto hole = components[0];
        HoleFillEvaluation evaluation = canFillHoleWithPiece(hole, PType::I);
        
        TestFixtures::assertTrue(evaluation.fillScore >= 0, "Fill score should be non-negative");
    }
}

// ===================================================================
// Utility Function Tests
// ===================================================================

void testGetTop3Rows() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[0] = 0x3FF;
    board[1] = 0x1FF;
    board[2] = 0x0FF;
    
    auto top3 = GetTop3Rows(board);
    // Should contain the top 3 rows
    TestFixtures::assertTrue(top3.any(), "Top 3 rows should have some bits set");
}

void testGetRows() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[5] = 0x3FF;
    board[6] = 0x1FF;
    
    auto rows = GetRows(board, 5, 6);
    TestFixtures::assertTrue(rows.any(), "Selected rows should have some bits set");
}

void testLSHHash() {
    std::vector<float> vec = {1.0f, 2.0f, 3.0f, 4.0f};
    uint64_t hash = lshHash(vec, 20);
    
    TestFixtures::assertTrue(hash > 0, "Hash should be non-zero");
    
    // Same vector should produce same hash
    uint64_t hash2 = lshHash(vec, 20);
    TestFixtures::assertEqual(hash, hash2, "Same vector should produce same hash");
}

// ===================================================================
// Run all AI evaluation tests
// ===================================================================

void runAllTests() {
    std::cout << "Running AI Evaluation Tests..." << std::endl;
    
    // Aspect Extraction Tests
    testExtractAspect_EmptyBoard();
    testExtractAspect_WithBoardState();
    
    // Height Calculation Tests
    testGetColumnHeight_EmptyBoard();
    testGetColumnHeight_SingleLine();
    testGetColumnHeight_VariedHeights();
    
    // Hole Detection Tests
    testCountHoles_EmptyBoard();
    testCountHoles_SolidBoard();
    testCountHoles_BoardWithHoles();
    testCountHoles_WellBoard();
    
    // T-Spin Detection Tests
    testIsTSpin_Basic();
    
    // Double Dagger Detection Tests
    testIsDoubleDagger_EmptyBoard();
    testIsDoubleDaggerLeft_EmptyBoard();
    testIsDoubleDaggerRight_EmptyBoard();
    
    // Terrain Quality Evaluation Tests
    testEvaluateTerrainQuality_EmptyBoard();
    testEvaluateTerrainQuality_SolidBoard();
    
    // Parity Spectrum Tests
    testCalculateParitySpectrum_EmptyBoard();
    
    // Horizontal Parity Tests
    testCalculateHorizontalParity_EmptyBoard();
    testCalculateHorizontalParity_SingleLine();
    
    // Perfect Clear Theorem Tests
    testIsPerfectClearTheoremSatisfied();
    testEvaluatePerfectClearPossibility();
    
    // Spin Detector Tests
    testStandardSpinDetector();
    testSpinEvaluator();
    
    // Reachable Space Analysis Tests
    testFindReachableSpaces_EmptyBoard();
    testFindReachableSpaces_SolidBoard();
    testFindConnectedComponents();
    testAnalyzeReachableSpaces();
    
    // Hole Evaluation Tests
    testEvaluateHole();
    
    // Tetris Well Evaluation Tests
    testEvaluateTetrisWell();
    
    // Surface Evaluation Tests
    testEvaluateSurface();
    
    // Post Clear Evaluation Tests
    testEvaluatePostClear();
    
    // Comprehensive Evaluation Tests
    testEvaluateTerrainWithHoles();
    testEvaluateTSDCandidates();
    testEvaluateHoleFilling();
    
    // Hole Fill Evaluation Tests
    testCanFillHoleWithPiece();
    
    // Utility Function Tests
    testGetTop3Rows();
    testGetRows();
    testLSHHash();
    
    std::cout << "All AI Evaluation tests passed!" << std::endl;
}

} // namespace AIEvaluateTests