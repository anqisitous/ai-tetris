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

// ---- 攻撃在キュー内の1件 ----
// 相殺処理を経て「相手へ実際に送る分」が確定した瞬間に生成される。
// 相殺はこの生成イベントの中で一回きり完結し、以後この攻撃は
// 送り主側の処理速度やタイミングに一切依存せず、着弾時刻だけで自律的に進む。
//
//   fireTime      : この攻撃が確定した瞬間の、送り主側ローカルの絶対ゲーム時刻。
//                   (セッション開始からの累積秒数。両者は同じ基準時計を使う前提)
//   travelTime    : 発生から着弾までの所要時間。全消し(パーフェクトクリア)の場合は0。
//                   着弾時刻は fireTime + travelTime で、受け主は自分のローカル
//                   時計がこれを超えたかどうかだけで判定でき、送り主の状態を
//                   ポーリングする必要はない。
//   baseCol       : 攻撃発生時に1回だけ乱数で決定される基準列(0-9)。
//                   その攻撃を通じて不変。
//   numMinosPlaced: 発生から着弾までの間に受け主がミノを置いた回数。
//                   受け主がロックするたびにインクリメントする。
struct PendingAttack {
    double fireTime = 0.0;
    double travelTime = 0.0;
    int damage = 0;
    int baseCol = 0;
    int numMinosPlaced = 0;
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
    // 送り主として保持する、まだ相手のincomingAttacksへ移していない攻撃。
    // (相殺後に相手へ送る分が確定した攻撃の一時置き場。dispatchで即座に
    //  相手のincomingAttacksへ移すだけなので、基本的に1フレームで空になる)
    std::vector<PendingAttack> outgoingAttacks;
    // 受け主として保持する、まだ着弾(盤面反映)されていない攻撃。
    // 着弾判定はfireTime+travelTimeの絶対時刻比較で行うため、
    // 送り主側の処理タイミングに依存せずローカルだけで進行できる。
    // 相殺はこのリストを直接いじる継続処理ではなく、自分が新しい攻撃を
    // 発生させる「その瞬間」に一度だけこのリストの残高とまとめて突き合わせる。
    // その相殺イベントより後に追加された攻撃は、次に自分が攻撃を出すまで
    // 相殺対象にならず、ディレイなしでそのまま着弾する。
    std::vector<PendingAttack> incomingAttacks;
    int linesCleared = 0;
    int damageBuff = 0;
    int score = 0;
    int level = 1;

    void init(int seed);
    PType popNext();
};

bool ShiftRowMask(uint16_t rowMask, int x, uint16_t& out);
bool IsCollision(const BoardBits& board, const MinoShape& shape, int x, int y);
int HardDropY(const BoardBits& board, const MinoShape& shape, int x);
int ClearLines(BoardBits& board);
int CalculateDamage(int linesCleared, bool tSpin, bool btb, int combo, bool perfectClear);

// ---- 旧来のガベージ追加 (単純な10%ホール移動方式。後方互換用) ----
void AddGarbage(BoardBits& board, int lines, std::mt19937& rng);

// ---- 新方式: 攻撃固有のbaseColとnumMinosPlacedから区間オフセットを
//      生成してガベージを追加する ----
// successProb: オフセット生成フェーズの各試行が成功する確率 (既定0.2 = 20%)
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

// ---- 攻撃の発生 (相殺込み・一回きり) ----
// 自分がロックでダメージを出した「その瞬間」にだけ呼ぶ。呼ばれるたびに
// 以下を一括で行う:
//   1. attackerが現在保持しているincomingAttacks(まだ着弾していない、
//      相手から来ている攻撃)の合計と、今回出す damage を突き合わせて相殺する。
//   2. 相殺後にincomingAttacks側に残った分(=打ち消しきれなかった相手の攻撃)は
//      ディレイなしで即座にattackerの盤面へ反映し、incomingAttacksから除去する。
//   3. 相殺後にdamage側に残った分は、fireTime=gameTimeNow・travelTimeを
//      持つ新しいPendingAttackとしてoutgoingAttacksへ積む(相手へ送る分)。
// この相殺は呼ばれた瞬間のincomingAttacksの残高だけを対象にした一回勝負であり、
// 呼び出しが終わった後に(自分がまだ次のミノを出していない間に)相手からの攻撃が
// 新たにincomingAttacksへ追加されても、それは今回の相殺には一切関与しない。
// isPerfectClear が true の場合、相手へ送る分のtravelTimeは0になる(即着弾)。
void fireAttack(PlayerState& attacker, int damage, double gameTimeNow,
                 double travelTime, bool isPerfectClear, std::mt19937& rng);

// ---- 受け主がミノを1つ置いたときに呼ぶ ----
// incomingAttacks内の全ての未着弾攻撃のnumMinosPlacedをインクリメントする。
void notifyMinoPlaced(PlayerState& receiver);

// ---- 着弾判定を進める ----
// fireTime + travelTime を自分のローカルgameTimeNowと比較するだけなので、
// 送り主側の処理速度やフレームレートには一切依存しない。
void advanceIncomingAttacks(PlayerState& receiver, double gameTimeNow, std::mt19937& rng);
