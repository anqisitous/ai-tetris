// ===================================================================
// ai_evaluate.h - Valutazione del terreno
// ===================================================================
#pragma once
#include "game_engine.h"
#include <bitset>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <queue>

// ============================================================
// 評価用の重み定数
// ============================================================
namespace EvalWeights {
    // 穴の評価
    constexpr float HOLE_AREA        = 8.0f;
    constexpr float HOLE_DEPTH       = 3.0f;
    constexpr float HOLE_COVERED     = 5.0f;
    constexpr float HOLE_SHAPE_BASE  = 10.0f;
    
    // テトリスWellの評価（非線形）
    constexpr float WELL_BASE_VALUE   = 80.0f;  // 深さ4の基礎価値
    constexpr float WELL_DEPTH_MARGIN = 10.0f;  // 深さ1増えるごとの追加価値
    constexpr int   WELL_MIN_DEPTH    = 4;      // テトリス可能な最小深さ
    constexpr int   WELL_MAX_DEPTH    = 8;      // 最大価値に達する深さ
    constexpr float WELL_I_BONUS      = 50.0f;
    
    // TSDの評価
    constexpr float TSD_SCORE        = 100.0f;
    constexpr float TSS_SCORE        = 50.0f;
    constexpr float TST_SCORE        = 150.0f;
    
    // 地形（Surface）の評価
    constexpr float VARIANCE_PENALTY = 2.0f;
    constexpr float CENTER_LOW_BONUS = 5.0f;
    
    // ライン消去後の評価
    constexpr float CLEAR_BONUS      = 20.0f;
    
    // パリティ評価
    constexpr float PARITY_PENALTY   = 500.0f;
    
    // 穴を埋めるピースのボーナス
    constexpr float HOLE_FILL_BONUS   = 30.0f;
    constexpr float PERFECT_FILL_BONUS = 50.0f;
}

// ============================================================
// 到達可能空間の連結成分
// ============================================================
struct ConnectedComponent {
    int left;    // 左端
    int right;   // 右端
    int top;     // 一番上
    int bottom;  // 一番下
    int width;   // 幅
    int height;  // 高さ
    std::vector<std::pair<int, int>> cells;  // 含まれるマス
};

// ============================================================
// 到達可能空間の解析結果
// ============================================================
struct ReachableSpaceInfo {
    std::vector<ConnectedComponent> components;
    std::vector<ConnectedComponent> tetrisWells;
    std::vector<ConnectedComponent> otherHoles;
};

// ============================================================
// 穴の評価構造体
// ============================================================
struct HoleEvaluation {
    float area;          // 面積（マス数）
    float maxDepth;      // 最大深さ
    float coveredCells;  // 上に覆われているセル数
    float shapePenalty;  // 形状ペナルティ
    float totalScore;    // 総評価スコア
};

// ============================================================
// テトリスの穴（Well）の評価構造体
// ============================================================
struct TetrisWellEvaluation {
    float depth;          // 井戸の深さ（連続した実際の深さ）
    float completeness;   // 完成度（0.0 ~ 1.0）
    float accessibility;  // 到達可能性
    float score;          // 総評価スコア
};

// ============================================================
// 穴を埋めるピースの評価
// ============================================================
struct HoleFillEvaluation {
    bool canFill;         // 穴を埋められるか
    bool isPerfectFill;   // 完全に埋められるか
    float fillScore;      // 埋めるスコア
};

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
// 段(row)を「埋まっているマス数が奇数か偶数か」で分類し、奇数パリティ段の数を返す。
// 盤面は10列なので、埋め切った段は必ず偶数(10マス)になる。
// 詳細な分類とミノ側の分類は pc_parity.h を参照。
int calculateHorizontalParity(const BoardBits& board);

// ---- Perfect Clear Parity (パフェのパリティ条件) ----
// 奇数パリティのミノ: I(90/270)
// 偶数パリティのミノ: I(0/180), O, S/Z(0/180)
// 2:2 のミノ        : T/J/L(全向き), S/Z(90/270)
// 条件: 奇数パリティ段は偶数個 (合計が奇数個ならパフェ不能) であり、
//       4*奇数ミノ + 2*(2:2ミノ) が奇数パリティ段の数以上であること。
//       さらに奇数パリティのミノが奇数個なら偶数パリティのミノも奇数個になる。
bool isPerfectClearTheoremSatisfied(int odd_minos, int even_minos, int mixed_minos,
                                    int odd_parity_rows);

// ネクスト(+ホールド)を使ってパフェのパリティ必要条件を満たせるかを評価する
// 満たせる: +100 / 満たせない: -100
float evaluatePerfectClearPossibility(const BoardBits& board, const std::vector<PType>& pieces);

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

    // SpinType に対応するスコアを取得（detector に委譲）
    float getScore(SpinType type, bool isBTB) const;
};

// ---- Reachable Space Analysis ----
// 到達可能な空マスをBFSで探索
std::vector<std::vector<bool>> findReachableSpaces(const BoardBits& board);

// 連結成分を抽出
std::vector<ConnectedComponent> findConnectedComponents(const std::vector<std::vector<bool>>& reachable);

// 到達可能空間を解析
ReachableSpaceInfo analyzeReachableSpaces(const BoardBits& board);

// 一番目に低い開いている穴を特定
ConnectedComponent getLowestReachableHole(const std::vector<ConnectedComponent>& holes);

// 穴を評価
HoleEvaluation evaluateHole(const ConnectedComponent& comp, const BoardBits& board);

// テトリスの穴（Well）の評価
TetrisWellEvaluation evaluateTetrisWell(const ConnectedComponent& comp, const BoardBits& board);

// 全ての穴を評価
float evaluateAllHoles(const BoardBits& board, const std::deque<PType>& next, bool hasHoldI);

// TSD候補を評価
float evaluateTSDCandidates(const BoardBits& board, const std::deque<PType>& next, bool hasHoldT);

// 地形（Surface）の評価
float evaluateSurface(const BoardBits& board);

// ライン消去後の維持
float evaluatePostClear(const BoardBits& board);

// 全てを統合した地形評価
float evaluateTerrainWithHoles(const BoardBits& board, const std::deque<PType>& next, bool hasHoldI, bool hasHoldT);

// ---- Hole Filling Evaluation ----
// 穴を埋めるピースの判定
HoleFillEvaluation canFillHoleWithPiece(const ConnectedComponent& hole, PType pieceType);

// 穴を埋める評価
float evaluateHoleFilling(const BoardBits& board, const std::deque<PType>& next, 
                         bool hasHoldI, bool hasHoldT, PType currentPiece);

// ---- Utilità ----
std::bitset<30> GetTop3Rows(const BoardBits& board);
std::bitset<30> GetRows(const BoardBits& board, int startRow, int endRow);
uint64_t lshHash(const std::vector<float>& vec, int bits = 20);

// ---- Danno e attacco ----
int calculateDamage(int linesCleared, bool tSpin, bool btb, int combo, bool perfectClear);
