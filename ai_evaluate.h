// ===================================================================
// ai_evaluate.h - Valutazione del terreno
// ===================================================================
#pragma once
#include "game_engine.h"
#include <bitset>
#include <vector>
#include <memory>

// ---- Aspetto del terreno (feature per AI) ----
struct Aspect {
    std::vector<float> values;
    BoardBits snapshot;
};

// ---- Funzioni di valutazione ----
Aspect extractAspect(const BoardBits& board, float timingDiff = 0.0f,
                     int combo = 0, const std::deque<PType>& next = {});

float aspectDistance(const Aspect& a, const Aspect& b);

// ---- Altezze e gruppi ----
int getColumnHeight(const BoardBits& board, int col);
float getGroupHeight(const BoardBits& board, int groupIdx);
std::array<int, 5> getGroupMinHeights(const BoardBits& board);

// ---- Rilevazione TSD / Double Dagger ----
bool isFilled(const BoardBits& board, int col, int row);
bool isTSpin(const BoardBits& board, int x, int y, int rot);
int countTSDDoubleSetups(const BoardBits& board);
bool isDoubleDaggerRight(const BoardBits& board);
bool isDoubleDaggerLeft(const BoardBits& board);
bool isDoubleDagger(const BoardBits& board);

// ---- Qualità del terreno ----
int countHoles(const BoardBits& board);
float evaluateTerrainQuality(const BoardBits& board);
float evaluateDoubleDaggerReadiness(const BoardBits& board);

// ---- Parità e spettro ----
float calculateParitySpectrum(const BoardBits& board);
bool isCenterOpen(const BoardBits& board);

// ---- Horizontal Parity (横パリティ) ----
int calculateHorizontalParity(const BoardBits& board);

// ---- Perfect Clear Theorem (パフェ定理) ----
// 定理: [JLTが0か180度の個数] = O個数 + [IZS*{±90度どちらか}の個数] + 2n + 現在の横パリティ
bool isPerfectClearTheoremSatisfied(int jlt_0_180_count, int o_count, int izs_pm90_count, int horizontal_parity);

// Evaluate perfect clear possibility using the theorem
float evaluatePerfectClearPossibility(const BoardBits& board, 
                                       int jlt_0_180_count, int o_count, int izs_pm90_count);

// ---- Spin Detection Polymorphism (T-Spin + Tetris) ----
// 回転後の状態を表す列挙型
enum class SpinType {
    NONE,       // 通常配置
    T_MINI,     // T-Spin Mini (0ライン)
    T_SINGLE,   // T-Spin Single (1ライン)
    T_DOUBLE,   // T-Spin Double (2ライン)
    T_TRIPLE,   // T-Spin Triple (3ライン)
    T_TETRIS,   // T-Spin Tetris (4ライン)
    TETRIS      // 通常のテトリス (4ライン)
};

// 回転判定の基底クラス
class SpinDetector {
public:
    virtual ~SpinDetector() = default;
    virtual SpinType detect(const BoardBits& board, const MinoShape& shape, int x, int y, int rot) const = 0;
    virtual float getScore(SpinType type, bool isBTB) const = 0;
};

// 具体的なT-Spin判定クラス
class StandardSpinDetector : public SpinDetector {
public:
    SpinType detect(const BoardBits& board, const MinoShape& shape, int x, int y, int rot) const override;
    float getScore(SpinType type, bool isBTB) const override;
};

// T-Spin評価マネージャー
class SpinEvaluator {
private:
    std::unique_ptr<SpinDetector> detector;
    bool considerBTB;

public:
    SpinEvaluator(bool btb = false);
    
    // T-Spinの可能性を評価
    float evaluate(const BoardBits& board, const std::deque<PType>& next);
    
    // BTB状態を考慮
    void setBTB(bool btb);
    
    // 具体的な配置のスピンタイプを取得
    SpinType getSpinType(const BoardBits& board, PType pieceType, int x, int y, int rot) const;
};

// ---- Utilità ----
std::bitset<30> GetTop3Rows(const BoardBits& board);
std::bitset<30> GetRows(const BoardBits& board, int startRow, int endRow);
uint64_t lshHash(const std::vector<float>& vec, int bits = 20);

// ---- Danno e attacco ----
int calculateDamage(int linesCleared, bool tSpin, bool btb, int combo, bool perfectClear);