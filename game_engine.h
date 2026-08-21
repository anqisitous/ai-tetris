// ===================================================================
// game_engine.h - Tetris Physics Engine (Immutabile)
// ===================================================================
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <deque>
#include <random>
#include <bitset>
#include <unordered_set>
#include <queue>
#include <unordered_map>

// For testing without SDL
#ifndef SDL_pixels_h_
struct SDL_Color {
    uint8_t r, g, b, a;
};
#endif

// ---- Costanti ----
constexpr int BOARD_W = 10;
constexpr int BOARD_H = 20;
constexpr int BOARD_BUFFER = 40;
constexpr int NUM_PTYPES = 7;
constexpr int CELL_SZ = 30;
constexpr int WIN_W = 940;
constexpr int WIN_H = 780;

// ---- Timing Constants ----
constexpr double VISUAL_SEND_DELAY = 1.0;    // Time from fire to visual arrival (seconds)
constexpr double TRAVEL_DELAY = 0.5;         // Time from visual arrival to actual arrival (seconds)
constexpr double COUNTER_DELAY = 1.3;       // Time from visual arrival to counter attack (seconds)

enum class PType : int { I = 0, O, T, S, Z, J, L, COUNT };
extern const PType ALL_TYPES[7];

enum class SpawnMode { Normal, Row21Escape };

extern const SDL_Color COLORS[NUM_PTYPES];

struct MinoShape {
    uint16_t rows[4];
    int height;
};

extern const MinoShape SHAPES[7][4];

extern const int8_t KICK_I[8][5][2];
extern const int8_t KICK_OTHER[8][5][2];

using BoardBits = std::array<uint16_t, BOARD_BUFFER>;

struct PlacementState {
    int x, y, rot;
    bool operator==(const PlacementState& o) const {
        return x == o.x && y == o.y && rot == o.rot;
    }
};

struct PlacementStateHash {
    size_t operator()(const PlacementState& s) const {
        return ((s.x * 31 + s.y) * 31 + s.rot);
    }
};

struct PlacementResult {
    BoardBits board;
    int damage;
    bool tSpin;
    int linesCleared;
    int x, y, rot;
    bool usedHold;
    int pathLength;
};

// ---- 攻撃待ちキューの1件 ----
// 相殺処理を経て「相手へ送る分」が確定した瞬間に生成されるが、
// 相殺はこの生成イベントの中で一回きり完了し、以降(自分が次に攻撃を出すまで)incomingAttacksへ追加されたまま残る。
//
//   fireTime      : この攻撃が確定した瞬間の、送り主側ローカルのゲーム時間 (セッション開始からの積算秒数。両者は同期時計を使う前提)
//                   (セッション開始からの積算秒数。両者は同期時計を使う前提)
//   travelTime    : 発生から着弾までの所要時間。全消し(パーフェクトクリア)の場合は0。
//                   着弾時刻は fireTime + travelTime で、受け手は自分のローカル
//                   時計がこれを超えたかどうかで判定でき、自分の処理速度やフレームレートに一片依存せず、
//                   ポーリングする必要はない。
//   baseCol       : 攻撃発生時に1度だけ乱数で決まる基準列(0-9)。
//                   その攻撃を通じて不変。
//   numMinosPlaced: 発生から着弾までの間に受け手がミノを置いた回数。
//                   受け手がロックするたびにインサメントし、穴バラ処理に利用する。
struct PendingAttack {
    double fireTime = 0.0;
    double travelTime = 0.0;
    double visualArriveTime = 0.0;
    int damage = 0;
    int baseCol = 0;
    int numMinosPlaced = 0;
    bool isVisuallyArrived = false;
    bool isCanceled = false;
};

struct PlayerState {
    BoardBits board = {};
    PType curType = PType::I;
    int curX = 3, curY = 0, curRot = 0;
    int pendingSpawnRotDelta = 0;
    int pendingSpawnXDelta = 0;
    SpawnMode lastSpawnMode = SpawnMode::Normal;
    std::vector<PType> bag, next;
    PType hold = PType::I;
    bool canHold = true, holdUsed = false;
    int combo = 0, btb = 0;
    bool gameOver = false;
    double tickAcc = 0;
    int garbageQueued = 0;
    double moveTimer = 0;
    int moveDir = 0;
    bool softDrop = false;
    // 送り主として保持する、まだ相手へ送る分が確定していない攻撃(=キュー先頭から送る予定の攻撃)
    // (相殺で相手へ送る分が確定した攻撃の一時保管場所。dispatchで即座に
    //  相手のincomingAttacksへ移し、基本的に1フレームで空になる)
    std::vector<PendingAttack> outgoingAttacks;
    // 受け手として保持する、まだ着弾(画面反映)されていない攻撃
    // 着弾判定はfireTime+travelTimeの絶対時間比較で行うため、
    // 送り主の処理速度やフレームレートに一片依存せず、ポーリングする必要はない。
    // 相殺はこのリストを繋ぎ直して行うため、連続して攻撃を受けた場合でも
    // 積まれた攻撃は、次に自分が攻撃を出すまでincomingAttacksへ追加されたまま残る。
    std::vector<PendingAttack> incomingAttacks;
    int linesCleared = 0;
    int damageBuff = 0;
    int score = 0;
    int level = 1;
    bool isLineClearing = false;  // ライン消去中か
    PendingAttack lastVisualAttack; // 最後に見かけ上届いた火力

    void init(int seed);
    PType popNext();
};

bool ShiftRowMask(uint16_t rowMask, int x, uint16_t& out);
bool IsCollision(const BoardBits& board, const MinoShape& shape, int x, int y);
int HardDropY(const BoardBits& board, const MinoShape& shape, int x);
int ClearLines(BoardBits& board);
int CalculateDamage(int linesCleared, bool tSpin, bool btb, int combo, bool perfectClear);

// ---- 旧来のガベージ追加 (単純な10%ホール移動方法、両方互換用) ----
void AddGarbage(BoardBits& board, int lines, std::mt19937& rng);

// ---- 新方法: 攻撃がbaseColとnumMinosPlacedから区域オフセットを
//      生成してガベージを追加する ----
// successProb: オフセット生成試行の各試行が成功する確率 (既定0.2 = 20%)
void AddGarbageWithOffset(BoardBits& board, int lines, int baseCol,
                           int numMinosPlaced, std::mt19937& rng,
                           float successProb = 0.2f);

std::vector<PlacementResult> EnumerateAllPlacements(
    const BoardBits& board, PType pieceType,
    bool canHold, PType holdType,
    int btb, int combo);

void stepPlayer(PlayerState& ps, double dt, double softDropSpeed);
void hardDropPlayer(PlayerState& ps);
void holdPiece(PlayerState& ps);
void applyMoveRepeat(PlayerState& ps, int dir, double dt, double das, double arr);
void finishLineClear(PlayerState& ps, int cleared, bool tSpin, bool perfectClear);
void lockAndSpawn(PlayerState& ps);

// ---- 攻撃の発生 (相殺処理・一括きり) ----
// 呼び出された瞬間のincomingAttacksの総和を相殺する。
// 連続的な視覚は行わない: この呼び出しが終われば相殺は完了し、以降(
// 積まれた攻撃は、次に自分が攻撃を出すまでincomingAttacksへ追加されたまま残る。
// isPerfectClear が true の場合、送る分travelTimeは0になる(即座)。
void fireAttack(PlayerState& attacker, int damage, double gameTimeNow,
                 double travelTime, bool isPerfectClear, std::mt19937& rng);

// ---- 受け手がミノを1つ置いたときに呼ばれる ----
// incomingAttacks内の全ての未着弾攻撃のnumMinosPlacedをインサメントする
void notifyMinoPlaced(PlayerState& receiver);

// ---- 着弾判定を進める ----
// fireTime + travelTime を受け手のローカルgameTimeNowと比較するだけで、
// 送り主の処理速度やフレームレートには一片依存しない。
void advanceIncomingAttacks(PlayerState& receiver, double gameTimeNow, std::mt19937& rng);
