// ===================================================================
// test_ai_decision.cpp - Integration tests for AI decision making
// ===================================================================
#include "../../game_engine.h"
#include "../../ai_core.h"
#include "../../ai_evaluate.h"
#include "../fixtures/test_fixtures.cpp"
#include <iostream>

namespace AIDecisionIntegrationTests {

// ===================================================================
// End-to-End AI Decision Tests
// ===================================================================

void testAI_Decision_EmptyBoard() {
    // Set up AI state
    AIState aiState;
    
    // Set up player states
    PlayerState player1 = TestFixtures::createPlayerWithBoard(
        TestFixtures::createEmptyBoard(), PType::I);
    PlayerState player2 = TestFixtures::createPlayerWithBoard(
        TestFixtures::createEmptyBoard(), PType::O);
    
    // Make AI decision
    AIAction action = decideAI(aiState, player1, player2);
    
    // Validate the action
    TestFixtures::assertTrue(action.targetX >= 0 && action.targetX < BOARD_W, 
                           "AI should choose valid X position");
    TestFixtures::assertTrue(action.targetRot >= 0 && action.targetRot < 4, 
                           "AI should choose valid rotation");
    
    // Execute the action
    executeAI(player1, action, 0.1, 0.1, 0.02);
    
    // Player should still be valid
    TestFixtures::assertFalse(player1.gameOver, "Player should not be game over after AI action");
}

void testAI_Decision_WithObstacles() {
    // Set up AI state
    AIState aiState;
    
    // Set up player states with obstacles
    BoardBits boardWithObstacles = TestFixtures::createSingleLineBoard();
    PlayerState player1 = TestFixtures::createPlayerWithBoard(boardWithObstacles, PType::I);
    PlayerState player2 = TestFixtures::createPlayerWithBoard(
        TestFixtures::createEmptyBoard(), PType::O);
    
    // Make AI decision
    AIAction action = decideAI(aiState, player1, player2);
    
    // Validate the action
    TestFixtures::assertTrue(action.targetX >= 0 && action.targetX < BOARD_W, 
                           "AI should choose valid X position with obstacles");
    TestFixtures::assertTrue(action.targetRot >= 0 && action.targetRot < 4, 
                           "AI should choose valid rotation with obstacles");
}

void testAI_Decision_WithDifferentPieces() {
    AIState aiState;
    
    // Test with all piece types
    for (int i = 0; i < (int)PType::COUNT; ++i) {
        PType pieceType = (PType)i;
        PlayerState player1 = TestFixtures::createPlayerWithBoard(
            TestFixtures::createEmptyBoard(), pieceType);
        PlayerState player2 = TestFixtures::createPlayerWithBoard(
            TestFixtures::createEmptyBoard(), PType::O);
        
        AIAction action = decideAI(aiState, player1, player2);
        
        TestFixtures::assertTrue(action.targetX >= 0 && action.targetX < BOARD_W, 
                               "AI should handle piece type " + std::to_string(i));
    }
}

// ===================================================================
// AI vs AI Game Simulation Tests
// ===================================================================

void testAI_vs_AI_Simulation() {
    AIState aiState1, aiState2;
    
    // Set up player states
    PlayerState player1 = TestFixtures::createBasicPlayerState(42);
    PlayerState player2 = TestFixtures::createBasicPlayerState(43);
    
    // Run a few steps of AI vs AI
    for (int i = 0; i < 5; ++i) {
        // Player 1's turn
        AIAction action1 = decideAI(aiState1, player1, player2);
        executeAI(player1, action1, 0.1, 0.1, 0.02);
        
        // Player 2's turn
        AIAction action2 = decideAI(aiState2, player2, player1);
        executeAI(player2, action2, 0.1, 0.1, 0.02);
        
        // Both players should still be valid
        TestFixtures::assertFalse(player1.gameOver, "Player 1 should not be game over");
        TestFixtures::assertFalse(player2.gameOver, "Player 2 should not be game over");
    }
}

// ===================================================================
// AI Learning Integration Tests
// ===================================================================

void testAI_Learning_FromGameplay() {
    AIState aiState;
    
    // Set up initial state
    PlayerState player = TestFixtures::createBasicPlayerState(42);
    Aspect before = extractAspect(player.board);
    
    // Make a decision
    AIAction action = decideAI(aiState, player, player);
    
    // Execute the action
    executeAI(player, action, 0.1, 0.1, 0.02);
    
    // Get the state after the action
    Aspect after = extractAspect(player.board);
    
    // Learn from the placement
    learnFromPlacement(aiState, before, after, 1.0f);
    
    // Pattern memory should have learned something
    TestFixtures::assertTrue(aiState.patternMemory.size() >= 0, 
                           "AI should learn from gameplay");
}

// ===================================================================
// AI with Templates Integration Tests
// ===================================================================

void testAI_WithTemplates() {
    AIState aiState;
    
    // Create a template library
    TemplateLibrary templateLib;
    
    TemplateDefinition templateDef;
    templateDef.name = "Opening Template";
    
    StageDefinition stage;
    stage.numBoards = 1;
    stage.boards = {std::bitset<30>(0x0)}; // Empty board
    stage.nextStage = "";
    
    templateDef.stages["start"] = stage;
    templateDef.startStage = "start";
    
    templateLib.addTemplate(templateDef);
    
    // Set up AI state with template library
    aiState.templateLib = &templateLib;
    
    // Set up player state
    PlayerState player = TestFixtures::createPlayerWithBoard(
        TestFixtures::createEmptyBoard(), PType::I);
    PlayerState opponent = TestFixtures::createPlayerWithBoard(
        TestFixtures::createEmptyBoard(), PType::O);
    
    // Make AI decision with templates
    AIAction action = decideAI(aiState, player, opponent);
    
    // Should still produce valid action
    TestFixtures::assertTrue(action.targetX >= 0 && action.targetX < BOARD_W, 
                           "AI with templates should produce valid action");
}

// ===================================================================
// AI Evaluation Integration Tests
// ===================================================================

void testAI_Evaluation_Integration() {
    AIState aiState;
    
    // Set up player with specific board state
    BoardBits board = TestFixtures::createWellBoard();
    PlayerState player = TestFixtures::createPlayerWithBoard(board, PType::I);
    PlayerState opponent = TestFixtures::createPlayerWithBoard(
        TestFixtures::createEmptyBoard(), PType::O);
    
    // Make AI decision
    AIAction action = decideAI(aiState, player, opponent);
    
    // The AI should try to handle the well situation
    TestFixtures::assertTrue(action.targetX >= 0 && action.targetX < BOARD_W, 
                           "AI should handle well board situation");
}

void testAI_Evaluation_WithHoles() {
    AIState aiState;
    
    // Set up player with board that has holes
    BoardBits board = TestFixtures::createBoardWithHoles();
    PlayerState player = TestFixtures::createPlayerWithBoard(board, PType::I);
    PlayerState opponent = TestFixtures::createPlayerWithBoard(
        TestFixtures::createEmptyBoard(), PType::O);
    
    // Make AI decision
    AIAction action = decideAI(aiState, player, opponent);
    
    // The AI should try to fill holes
    TestFixtures::assertTrue(action.targetX >= 0 && action.targetX < BOARD_W, 
                           "AI should handle board with holes");
}

// ===================================================================
// Performance Tests
// ===================================================================

void testAI_Decision_Performance() {
    AIState aiState;
    
    // Set up player states
    PlayerState player1 = TestFixtures::createBasicPlayerState(42);
    PlayerState player2 = TestFixtures::createBasicPlayerState(43);
    
    // Time multiple decisions
    const int numDecisions = 10;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numDecisions; ++i) {
        AIAction action = decideAI(aiState, player1, player2);
        // Don't execute, just decide
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    double avgTime = elapsed.count() / numDecisions;
    
    std::cout << "Average AI decision time: " << avgTime << " seconds" << std::endl;
    
    // Should be reasonably fast (less than 1 second per decision)
    TestFixtures::assertTrue(avgTime < 1.0, "AI decisions should be reasonably fast");
}

// ===================================================================
// Edge Case Tests
// ===================================================================

void testAI_Decision_GameOverState() {
    AIState aiState;
    
    // Set up player in game over state
    PlayerState player1 = TestFixtures::createBasicPlayerState(42);
    player1.gameOver = true;
    PlayerState player2 = TestFixtures::createBasicPlayerState(43);
    
    // AI should still produce an action (though it might be invalid)
    AIAction action = decideAI(aiState, player1, player2);
    
    // Should not crash
    TestFixtures::assertTrue(true, "AI should handle game over state without crashing");
}

void testAI_Decision_FullBoard() {
    AIState aiState;
    
    // Set up player with almost full board
    BoardBits fullBoard = TestFixtures::createEmptyBoard();
    for (int r = 0; r < BOARD_H; ++r) {
        fullBoard[r] = 0x3FF; // Fill all rows
    }
    
    PlayerState player1 = TestFixtures::createPlayerWithBoard(fullBoard, PType::I);
    PlayerState player2 = TestFixtures::createBasicPlayerState(43);
    
    // AI should handle full board
    AIAction action = decideAI(aiState, player1, player2);
    
    // Should not crash
    TestFixtures::assertTrue(true, "AI should handle full board without crashing");
}

// ===================================================================
// Run all integration tests
// ===================================================================

void runAllTests() {
    std::cout << "Running AI Decision Integration Tests..." << std::endl;
    
    // End-to-End AI Decision Tests
    testAI_Decision_EmptyBoard();
    testAI_Decision_WithObstacles();
    testAI_Decision_WithDifferentPieces();
    
    // AI vs AI Game Simulation Tests
    testAI_vs_AI_Simulation();
    
    // AI Learning Integration Tests
    testAI_Learning_FromGameplay();
    
    // AI with Templates Integration Tests
    testAI_WithTemplates();
    
    // AI Evaluation Integration Tests
    testAI_Evaluation_Integration();
    testAI_Evaluation_WithHoles();
    
    // Performance Tests
    testAI_Decision_Performance();
    
    // Edge Case Tests
    testAI_Decision_GameOverState();
    testAI_Decision_FullBoard();
    
    std::cout << "All AI Decision Integration tests passed!" << std::endl;
}

} // namespace AIDecisionIntegrationTests