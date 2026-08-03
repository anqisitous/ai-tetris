// ===================================================================
// ai_core.h - AI Decision Making
// ===================================================================
#pragma once
#include "game_engine.h"
#include "ai_evaluate.h"
#include "ai_templates.h"
#include "pc_search.h"
#include <vector>
#include <deque>
#include <memory>

// ---- Azione dell'AI ----
struct AIAction {
    int targetX = 0;
    int targetRot = 0;
    bool shouldHold = false;
    bool shouldDrop = false;
    bool ready = false;
    bool holdDone = false;
    
    // Per apprendimento
    int predictedDamage = 0;
    Aspect predictedOppBoard;
    Aspect beforeBoard;
    bool hasPrediction = false;
};

// ---- Memoria pattern dinamici (max 70) ----
struct PatternNeuron {
    Aspect targetAspect;
    std::vector<PType> bagSequence;
    int depth;
    int confidence;
    uint64_t lshHash;
};

class PatternMemory {
private:
    std::vector<PatternNeuron> patterns;  // Max 70
    
public:
    void addPattern(const PatternNeuron& p);
    PatternNeuron* findBestMatch(const Aspect& current);
    size_t size() const { return patterns.size(); }
};

// ---- Stato dell'AI ----
struct AIState {
    PatternMemory patternMemory;
    ActiveTemplate* activeTemplate = nullptr;
    TemplateLibrary* templateLib = nullptr;
    
    double dasDelay = 0.10;
    double arrDelay = 0.02;
    double thinkInterval = 0.10;
    double thinkTimer = 0.0;

    // パフェ探索
    bool enablePerfectClear = true;
    PCSearchOptions pcOptions;
};

// ---- Funzioni AI ----
AIAction decideAI(AIState& state, PlayerState& self, PlayerState& opp);
void executeAI(PlayerState& ps, AIAction& act, double dt, double das, double arr);
AIAction makeAction(const PlacementResult& best, bool usedHold);

// ---- Valutazione di un singolo piazzamento (usata da decideAI e beam search) ----
// beforeBoard: 配置前の盤面（T-Spin判定に必要）
// curType: 配置するミノの種類（T-Spin判定に必要）
float evaluateCandidate(AIState& state, const PlacementResult& c,
                         const BoardBits& beforeBoard, PType curType,
                         const std::deque<PType>& next,
                         PType hold, int btb, int combo);

// ---- 実戦用の指し手決定 ----
// パフェが狙えるならその手順を優先し、それ以外は Beam Search を使う。
AIAction decideAIMove(AIState& state, PlayerState& self);

// ---- Perfect Clear ----
// 盤面が低いときにパフェ手順を探し、見つかれば最初の一手を返す。
// 見つからなければ ready = false の AIAction を返す。
AIAction tryPerfectClearAction(AIState& state, const PlayerState& self);

// ---- Beam Search ----
// 現在の盤面から複数手先まで探索し、最初の一手を決定する。
struct BeamNode {
    BoardBits board;              // この時点の盤面
    std::deque<PType> next;       // 残りネクストキュー（先頭から消費）
    PType hold;                   // 保持中のミノ
    bool canHold;                 // ホールド使用可能か
    int btb;                      // Back-to-Back カウント
    int combo;                    // コンボ数
    float score = 0.0f;           // ここまでの累積評価スコア

    // ルート直下（1手目）の情報。最終的な行動選択に使う。
    bool hasFirstMove = false;
    int firstX = 0, firstY = 0, firstRot = 0;
    bool firstUsedHold = false;
    PlacementResult firstResult{};
};

// beamWidth: 各深さで残すノード数
// searchDepth: 何手先まで読むか（1なら decideAI の単発評価と同等）
BeamNode beamSearch(AIState& state, PlayerState& self,
                     int beamWidth = 20, int searchDepth = 3);

// beamSearch の結果から AIAction を組み立てる
AIAction makeActionFromBeam(const BeamNode& node);

// ---- Apprendimento ----
void learnFromPlacement(AIState& state, const Aspect& before, 
                         const Aspect& after, float reward);