// ===================================================================
// ai_neural.h - 予測ニューロン型AI（報酬/罰なし・純粋予測）
// ===================================================================
#pragma once
#include "game_engine.h"
#include "ai_evaluate.h"
#include "pc_parity.h"
#include <vector>
#include <deque>
#include <array>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <string>

// ============================================================
// 前方宣言
// ============================================================
struct PredictionNeuron;
struct NeuralAIState;

// ============================================================
// 予測ニューロン（アスペクト = 予測値）
// ============================================================
struct PredictionNeuron {
    // ---- 20段階観測（単一入力層） ----
    std::array<float, 20> rawObservation{};  // 生の盤面観測
    
    // ---- 相手地形分析 ----
    struct OpponentTerrain {
        int tallerSide = 0;           // 1=左, 2=右, 0=均等
        int firstLowestOpenRow = -1;  // 一番目に低い開いてる段
        int secondLowestOpenRow = -1; // 二番目に低い開いてる段
        bool canFillFlatFromFirst = false;   // 一番目から平らに埋められるか
        bool canFillFlatFromSecond = false;  // 二番目から平らに埋められるか
        float flatFillScore = 0.0f;   // 平ら埋めスコア（0.0-1.0）
        int stepsToFill = 0;          // 埋めるのに必要な手数
        std::string reason;           // 判定理由
    } oppTerrain;
    
    // ---- TST/TSD準備地形 ----
    struct TSetup {
        int tSetupCount = 0;          // Tで埋められる地形の数
        int topRightFilled = 0;       // 右上が埋まっている数
        int topLeftFilled = 0;        // 左上が埋まっている数
        float tSetupScore = 0.0f;     // 準備スコア
        std::vector<int> tSetupPositions;  // 準備位置 (x,y 交互)
    } tSetup;
    
    // ---- 相手ミノ有利判定 ----
    struct OpponentAdvantage {
        bool hasI = false;            // I持ってるか
        bool hasT = false;            // T持ってるか
        bool hasLJ = false;           // L/J持ってるか
        float advantageScore = 0.0f;  // -1.0 ~ +1.0
        int nextPieceCount = 0;       // 次のミノ数
    } oppAdvantage;
    
    // ---- 隠れ層（自律的に階層構築） ----
    std::vector<float> hiddenLayer1;  // 第1隠れ層
    std::vector<float> hiddenLayer2;  // 第2隠れ層
    std::vector<float> hiddenLayer3;  // 第3隠れ層
    
    // ---- 予測値（アスペクト = これ） ----
    struct PredictedValues {
        float nextBoardSimilarity = 0.5f;   // 次の盤面の類似度予測
        float chainContinuation = 0.5f;     // チェーン継続確率
        float damagePotential = 0.0f;       // 火力ポテンシャル
        float timingFactor = 0.5f;          // タイミング係数
        float opponentReaction = 0.5f;      // 相手の反応予測
    } prediction;
    
    // ---- ペア記憶 ----
    struct PairMemory {
        // [自分→自分]
        struct SelfPair {
            VariantSignature before;
            VariantSignature after;
            float relation = 1.0f;
        } selfPair;
        
        // [現在の自分→次の相手]
        struct OppPair {
            VariantSignature selfCurrent;
            VariantSignature oppNext;
            float relation = 1.0f;
        } oppPair;
        
        // タイミング関連（<1.0）
        struct TimingRelation {
            float fireStart = 0.0f;
            float fireCreated = 0.0f;
            float fireSent = 0.0f;
            float relation = 0.5f;
        } timing;
    } pairMemory;
    
    // ---- LSHハッシュ ----
    uint64_t lshHash = 0;
    
    // ---- 類似度計算 ----
    float similarity(const PredictionNeuron& other) const;
};

// ============================================================
// 予測ニューロンAI状態
// ============================================================
struct NeuralAIState {
    // ---- ニューロンネットワーク ----
    std::vector<PredictionNeuron> neurons;  // 全ニューロン
    std::vector<int> layerStructure;        // 層構造 [入力, 隠れ1, 隠れ2, ...]
    
    // ---- 活性化ニューロン ----
    int activeNeuronIndex = -1;
    PredictionNeuron::PredictedValues lastError;  // 最後の予測誤差
    
    // ---- 学習パラメータ ----
    float similarityThreshold = 0.7f;
    float timingDecay = 0.95f;
    int maxNeurons = 200;
    
    // ---- 統計 ----
    int totalPredictions = 0;
    int correctPredictions = 0;
    float accuracy = 0.0f;
    
    // ---- 自律的層拡張 ----
    bool autoExpandLayers = true;
    int maxHiddenLayers = 5;
    int neuronsPerLayer = 64;
    
    // ---- 履歴 ----
    std::vector<PredictionNeuron> predictionHistory;  // 過去の予測
    int historyMax = 100;
};

// ============================================================
// コア関数宣言
// ============================================================

// ---- 観測生成 ----
std::array<float, 20> generateObservation(const BoardBits& board, const std::deque<PType>& next, int combo, float gameTime);

// ---- tallerSide埋め可能性評価 ----
struct TallerSideFillability {
    bool canFill = false;
    float fillScore = 0.0f;
    int stepsToFill = 0;
    std::string reason;
};
TallerSideFillability evaluateTallerSideFillability(const BoardBits& board, int tallerSide);

// ---- 相手地形分析 ----
PredictionNeuron::OpponentTerrain analyzeOpponentTerrain(const BoardBits& board);

// ---- TST/TSD準備分析 ----
PredictionNeuron::TSetup analyzeTSetup(const BoardBits& board);

// ---- 相手ミノ有利判定 ----
PredictionNeuron::OpponentAdvantage analyzeOpponentAdvantage(const std::deque<PType>& next, PType hold);

// ---- ニューロン生成 ----
PredictionNeuron createPredictionNeuron(const BoardBits& board, const std::deque<PType>& next, PType hold, int combo, float gameTime);

// ---- ニューロン活性化 ----
int activateNeuron(NeuralAIState& state, const PredictionNeuron& current);

// ---- 予測実行 ----
PredictionNeuron::PredictedValues predictNext(const PredictionNeuron& current, const PredictionNeuron& target);

// ---- ペア記憶 ----
void storePairMemory(PredictionNeuron& neuron, const VariantSignature& selfBefore, const VariantSignature& selfAfter, const VariantSignature& oppNext, float fireStartTime, float fireCreatedTime, float fireSentTime);

// ---- 自律的層拡張 ----
void autoExpandLayers(NeuralAIState& state, const PredictionNeuron& current, const PredictionNeuron::PredictedValues& error);

// ---- メインAI決定 ----
AIAction decideNeuralAI(NeuralAIState& state, PlayerState& self, PlayerState& opp, float gameTime, float fireStartTime, float fireCreatedTime, float fireSentTime);

// ---- 学習フィードバック ----
void feedbackNeuralAI(NeuralAIState& state, PlayerState& self, PlayerState& opp, float gameTime, const BoardBits& prevBoard);

// ---- 保存/読込 ----
void saveNeuralState(const NeuralAIState& state, const std::string& path);
void loadNeuralState(NeuralAIState& state, const std::string& path);