// ===================================================================
// simple_test_runner.cpp - Simple test runner for AI Tetris
// ===================================================================

// Include test config first
#include "test_config.h"

// Include the main headers
#include "../game_engine.h"
#include "../ai_evaluate.h"
#include "../ai_core.h"
#include "../pc_parity.h"
#include "../pc_search.h"

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
    
    // row < 0 はバッファ領域（画面上部の見えない部分）として扱われ、
    // IsCollision内では意図的に衝突なしとされる(game_engine.cpp: ShiftRowMask/IsCollision参照)。
    // スポーン位置での画面外はみ出しは、main_sdl3.cppのcheckSpawnCollisionが別途補完する設計であり、
    // IsCollision単体がy<0で衝突を返さないのは仕様通りの挙動。
    collision = IsCollision(board, SHAPES[(int)PType::I][0], 3, -1);
    if (collision) throw std::runtime_error("IsCollision should not flag y<0 (buffer zone) as collision by design");
}

void testHardDropY_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    
    // I piece (spawn rotation, height=1) は空盤面ではBOARD_H-1まで落下する
    // (heightが1マス分のみのため、底はBOARD_H-1)
    int dropY = HardDropY(board, SHAPES[(int)PType::I][0], 3);
    if (dropY != BOARD_H - 1) {
        throw std::runtime_error("I piece (height=1) should drop to y=BOARD_H-1 on empty board");
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
    // Tetris Guideline準拠のダメージテーブル: シングル消去(1ライン)は攻撃力0。
    // ダブル=1, トリプル=2, テトリス=4 (game_engine.cpp: CalculateDamage参照)。
    int damage = CalculateDamage(1, false, false, 0, false);
    if (damage != 0) {
        throw std::runtime_error("Single line clear should deal 0 damage (Tetris Guideline)");
    }

    int doubleDamage = CalculateDamage(2, false, false, 0, false);
    if (doubleDamage != 1) {
        throw std::runtime_error("Double line clear should deal 1 damage");
    }

    int tetrisDamage = CalculateDamage(4, false, false, 0, false);
    if (tetrisDamage != 4) {
        throw std::runtime_error("Tetris (4 lines) should deal 4 damage");
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
// Horizontal Parity Tests (横パリティ)
// ===================================================================

namespace ParityTests {

// 列番号の集をマスクにする
uint16_t rowMask(const std::vector<int>& cols) {
    uint16_t mask = 0;
    for (int c : cols) mask |= static_cast<uint16_t>(1 << c);
    return mask;
}

// 指定列を除いて埋めた段
uint16_t fullRowExcept(const std::vector<int>& emptyCols) {
    uint16_t mask = 0x3FF;
    for (int c : emptyCols) mask &= static_cast<uint16_t>(~(1 << c));
    return mask;
}

// ミノの分類が仕様と一致するか
void testMinoParityClassification() {
    for (int rot = 0; rot < 4; ++rot) {
        bool vertical = (rot % 2 == 1);

        MinoParity expectedI = vertical ? MinoParity::Odd : MinoParity::Even;
        TestFixtures::assertTrue(getMinoParity(PType::I, rot) == expectedI,
                                 "I piece parity classification");

        TestFixtures::assertTrue(getMinoParity(PType::O, rot) == MinoParity::Even,
                                 "O piece must be even parity");

        MinoParity expectedSZ = vertical ? MinoParity::Mixed : MinoParity::Even;
        TestFixtures::assertTrue(getMinoParity(PType::S, rot) == expectedSZ,
                                 "S piece parity classification");
        TestFixtures::assertTrue(getMinoParity(PType::Z, rot) == expectedSZ,
                                 "Z piece parity classification");

        for (PType t : {PType::T, PType::J, PType::L}) {
            TestFixtures::assertTrue(getMinoParity(t, rot) == MinoParity::Mixed,
                                     "T/J/L pieces must be 2:2");
        }
    }

    // 奇数パリティは4段、偶数は0段、2:2は2段に奇数マスを足す
    TestFixtures::assertEqual(4, countOddRowContributions(PType::I, 1), "I(90) covers 4 rows");
    TestFixtures::assertEqual(0, countOddRowContributions(PType::I, 0), "I(0) covers 0 rows");
    TestFixtures::assertEqual(2, countOddRowContributions(PType::T, 0), "T covers 2 rows");

    // 奇数パリティになれるのは I だけ
    for (PType t : ALL_TYPES) {
        bool expected = (t == PType::I);
        TestFixtures::assertTrue(canTakeParity(t, MinoParity::Odd) == expected,
                                 "Only I can be odd parity");
    }
}

// 空の盤面は奇数パリティ段が0
void testHorizontalParity_EmptyBoard() {
    BoardBits board = TestFixtures::createEmptyBoard();
    TestFixtures::assertEqual(0, calculateHorizontalParity(board),
                              "Empty board has no odd parity rows");
}

// 1マス空きの奇数パリティ段が偶数個の盤面ケース
void testHorizontalParity_OddRowsMustBeEven() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[19] = fullRowExcept({4});  // 9マス -> 奇数パリティ
    board[18] = fullRowExcept({7});  // 9マス -> 奇数パリティ
    board[17] = rowMask({0, 1, 2, 3, 4, 5});  // 6マス -> 偶数パリティ

    TestFixtures::assertEqual(2, calculateHorizontalParity(board),
                              "Two rows with a single gap are odd parity");

    HorizontalParityInfo info = analyzeHorizontalParity(board, 16, 19);
    TestFixtures::assertEqual(2, info.oddRows, "Odd parity rows in region");
    TestFixtures::assertEqual(16, info.emptyCells, "Empty cells in region");
    TestFixtures::assertEqual(4, info.minoBudget, "Minos needed to fill region");
    TestFixtures::assertTrue(info.valid(), "Region must be a valid perfect clear case");

    // 奇数パリティ段が奇数個になる盤面は埋め切れない
    BoardBits odd = TestFixtures::createEmptyBoard();
    odd[19] = fullRowExcept({4});
    HorizontalParityInfo oddInfo = analyzeHorizontalParity(odd, 19, 19);
    TestFixtures::assertEqual(1, oddInfo.oddRows, "Single gap row is odd parity");
    TestFixtures::assertFalse(oddInfo.valid(), "Odd number of odd parity rows cannot be filled");
}

// 1つの盤面ケースに対して12通りの内訳を列挙する
void testEnumerateParityCombinations() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[19] = fullRowExcept({4});
    board[18] = fullRowExcept({7});
    board[17] = rowMask({0, 1, 2, 3, 4, 5});

    HorizontalParityInfo info = analyzeHorizontalParity(board, 16, 19);
    std::vector<ParityCombination> combos = enumerateParityCombinations(info);

    TestFixtures::assertEqual(12, static_cast<int>(combos.size()),
                              "12 parity combinations should be listed");

    for (const ParityCombination& c : combos) {
        TestFixtures::assertEqual(info.minoBudget, c.totalMinos(),
                                  "Combination must use exactly the needed minos");
        TestFixtures::assertTrue(c.oddRowCoverage() >= info.oddRows,
                                 "Combination must cover every odd parity row");
    }

    // 奇数ミノが奇数個なら偶数ミノも奇数個という規則を満たすものが先頭に来る
    TestFixtures::assertTrue(combos.front().followsOddCountRule(),
                             "Rule-satisfying combinations come first");

    // I が1つもなければ奇数ミノを含む内訳は組めない
    std::vector<PType> noI = {PType::O, PType::T, PType::J, PType::L};
    ParityCombination withOdd;
    withOdd.oddMinos = 1;
    withOdd.evenMinos = 1;
    withOdd.mixedMinos = 2;
    TestFixtures::assertFalse(isCombinationReachable(noI, withOdd),
                              "Odd parity minos require an I piece");

    std::vector<PType> withI = {PType::I, PType::O, PType::T, PType::J};
    TestFixtures::assertTrue(isCombinationReachable(withI, withOdd),
                             "I + O + T/J can form odd/even/mixed 1/1/2");
}

// パリティ定理の判定
void testPerfectClearTheorem() {
    // 奇数パリティ段が奇数個 -> 不可
    TestFixtures::assertFalse(isPerfectClearTheoremSatisfied(1, 1, 2, 3),
                              "Odd count of odd parity rows is impossible");
    // 奇数ミノ1個 + 偶数ミノ1個 + 2:2ミノ2個 で 2段を解消
    TestFixtures::assertTrue(isPerfectClearTheoremSatisfied(1, 1, 2, 2),
                             "Odd minos odd count with odd even-mino count is allowed");
    // 奇数ミノが奇数個なのに偶数ミノが偶数個 -> 規則違反
    TestFixtures::assertFalse(isPerfectClearTheoremSatisfied(1, 2, 1, 2),
                              "Odd mino count must match even mino count parity");
    // 奇数パリティ段を解消できない内訳 -> 不可
    TestFixtures::assertFalse(isPerfectClearTheoremSatisfied(0, 4, 0, 2),
                              "Even parity minos alone cannot fix odd parity rows");
}

} // namespace ParityTests

// ===================================================================
// Perfect Clear Search Tests (パフェ探索)
// ===================================================================

namespace PCSearchTests {

// 手順を盤面に適用する
BoardBits applyMove(const BoardBits& board, const PCMove& move) {
    BoardBits next = board;
    const MinoShape& shape = SHAPES[static_cast<int>(move.type)][move.rot];
    for (int r = 0; r < shape.height; ++r) {
        uint16_t mask = 0;
        if (!ShiftRowMask(shape.rows[r], move.x, mask)) {
            throw std::runtime_error("Move places a mino outside the board");
        }
        if (mask == 0) continue;
        int row = move.y + r;
        if (row < 0 || row >= BOARD_H) throw std::runtime_error("Move places a mino outside the board");
        if (next[row] & mask) throw std::runtime_error("Move overlaps existing blocks");
        next[row] |= mask;
    }
    ClearLines(next);
    return next;
}

void replayAndExpectPerfectClear(const BoardBits& board, const PCSearchResult& result) {
    BoardBits current = board;
    for (const PCMove& move : result.moves) current = applyMove(current, move);
    for (int r = 0; r < BOARD_H; ++r) {
        if (current[r] != 0) throw std::runtime_error("Replaying the solution must empty the board");
    }
}

// 1段だけ残った盤面を I ミノ1つでパフェ
void testFindPerfectClear_SinglePiece() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[19] = ParityTests::rowMask({0, 1, 2, 3, 4, 5});

    std::deque<PType> queue = {PType::I};
    PCSearchResult result = findPerfectClear(board, queue, PType::O, false);

    TestFixtures::assertTrue(result.found, "I piece should complete the last row");
    TestFixtures::assertEqual(1, static_cast<int>(result.moves.size()), "One mino is enough");
    replayAndExpectPerfectClear(board, result);
}

// 2段×6列の穴を O ミノ3つでパフェ
void testFindPerfectClear_MultiPiece() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[19] = ParityTests::rowMask({0, 1, 2, 3});
    board[18] = ParityTests::rowMask({0, 1, 2, 3});

    std::deque<PType> queue = {PType::O, PType::O, PType::O};
    PCSearchResult result = findPerfectClear(board, queue, PType::I, false);

    TestFixtures::assertTrue(result.found, "Three O minos should clear two rows");
    TestFixtures::assertEqual(3, static_cast<int>(result.moves.size()), "Three minos are needed");
    replayAndExpectPerfectClear(board, result);
}

// ホールドを使わないと解けないケース
void testFindPerfectClear_UsesHold() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[19] = ParityTests::rowMask({0, 1, 2, 3, 4, 5});

    std::deque<PType> queue = {PType::S};

    PCSearchResult withoutHold = findPerfectClear(board, queue, PType::I, false);
    TestFixtures::assertFalse(withoutHold.found, "S piece alone cannot fill a flat 4 wide row");

    PCSearchResult withHold = findPerfectClear(board, queue, PType::I, true);
    TestFixtures::assertTrue(withHold.found, "Held I piece should be used");
    TestFixtures::assertTrue(withHold.moves.front().usedHold, "First move must come from hold");
    replayAndExpectPerfectClear(board, withHold);
}

// パリティ的に不可能な盤面は探索しない
void testPerfectClearChance_ImpossibleCase() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[19] = ParityTests::fullRowExcept({4});  // 空き1マス -> 4の倍数でない

    std::deque<PType> queue = {PType::I, PType::O, PType::T, PType::S, PType::Z};
    TestFixtures::assertFalse(hasPerfectClearChance(board, queue, PType::J, true),
                              "A single empty cell cannot be filled by 4 cell minos");

    PCSearchResult result = findPerfectClear(board, queue, PType::J, true);
    TestFixtures::assertFalse(result.found, "Search must not report a solution");
}

// 空きマスの連結成分のチェック
void testEmptyRegionsDivisibleByFour() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[19] = ParityTests::rowMask({0, 1, 2, 3, 4, 5});
    TestFixtures::assertTrue(emptyRegionsDivisibleByFour(board, 19),
                             "A 4 cell gap is fillable");

    BoardBits split = TestFixtures::createEmptyBoard();
    split[19] = ParityTests::rowMask({0, 1, 2, 4, 5, 6, 7, 8});
    TestFixtures::assertFalse(emptyRegionsDivisibleByFour(split, 19),
                              "Isolated gaps of 1 and 1 cannot be filled");
}

// AI がパフェ手順を選ぶ
void testAI_UsesPerfectClear() {
    BoardBits board = TestFixtures::createEmptyBoard();
    board[19] = ParityTests::rowMask({0, 1, 2, 3, 4, 5});

    AIState state;
    PlayerState player = TestFixtures::createPlayerWithBoard(board, PType::I);
    player.next.clear();
    player.next.push_back(PType::O);
    player.canHold = false;

    AIAction action = tryPerfectClearAction(state, player);
    TestFixtures::assertTrue(action.ready, "AI should find the perfect clear");
    TestFixtures::assertEqual(6, action.targetX, "I piece must fill columns 6-9");
    TestFixtures::assertFalse(action.shouldHold, "Hold is not needed here");
}

} // namespace PCSearchTests

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
    
    // Horizontal Parity Tests
    std::cout << "\n--- Horizontal Parity Tests ---" << std::endl;
    runTest("Mino Parity Classification", ParityTests::testMinoParityClassification);
    runTest("Horizontal Parity - Empty Board", ParityTests::testHorizontalParity_EmptyBoard);
    runTest("Horizontal Parity - Odd Rows Must Be Even", ParityTests::testHorizontalParity_OddRowsMustBeEven);
    runTest("Parity Combinations - 12 Ways", ParityTests::testEnumerateParityCombinations);
    runTest("Perfect Clear Theorem", ParityTests::testPerfectClearTheorem);

    // Perfect Clear Search Tests
    std::cout << "\n--- Perfect Clear Search Tests ---" << std::endl;
    runTest("Perfect Clear - Single Piece", PCSearchTests::testFindPerfectClear_SinglePiece);
    runTest("Perfect Clear - Multi Piece", PCSearchTests::testFindPerfectClear_MultiPiece);
    runTest("Perfect Clear - Uses Hold", PCSearchTests::testFindPerfectClear_UsesHold);
    runTest("Perfect Clear - Impossible Case", PCSearchTests::testPerfectClearChance_ImpossibleCase);
    runTest("Perfect Clear - Empty Regions", PCSearchTests::testEmptyRegionsDivisibleByFour);
    runTest("Perfect Clear - AI Integration", PCSearchTests::testAI_UsesPerfectClear);

    // Integration Tests
    std::cout << "\n--- Integration Tests ---" << std::endl;
    runTest("AI Decision - Basic", IntegrationTests::testAI_Decision_Basic);
    
    // Print summary
    printSummary();
    
    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}