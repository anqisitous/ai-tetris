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

// ---- Colori SDL ----
extern const SDL_Color COLORS[NUM_PTYPES];

// ---- Forme dei pezzi ----
struct MinoShape {
    uint16_t rows[4];
    int height;
};

extern const MinoShape SHAPES[7][4];

// ---- Tabelle SRS Kick ----
extern const int8_t KICK_I[8][5][2];
extern const int8_t KICK_OTHER[8][5][2];

// ---- Board a bit ----
using BoardBits = std::array<uint16_t, BOARD_BUFFER>;

// ---- Stato di piazzamento ----
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

// ---- Risultato piazzamento ----
struct PlacementResult {
    BoardBits board;
    int damage;
    bool tSpin;
    int linesCleared;
    int x, y, rot;
    bool usedHold;
    int pathLength;
};

// ---- Attacco in coda ----
struct PendingAttack {
    double timeLeft;
    int damage;
};

// ---- Stato del giocatore ----
struct PlayerState {
    BoardBits board = {};
    PType curType = PType::I;
    int curX = 3, curY = 0, curRot = 0;
    std::deque<PType> bag, next;
    PType hold = PType::I;
    bool canHold = true, holdUsed = false;
    int combo = 0, btb = 0;
    bool gameOver = false;
    double tickAcc = 0;
    int garbageQueued = 0;
    double moveTimer = 0;
    int moveDir = 0;
    bool softDrop = false;
    std::vector<PendingAttack> outgoingAttacks;
    int linesCleared = 0;
    int damageBuff = 0;
    int score = 0;
    int level = 1;

    void init(int seed);
    PType popNext();
};

// ---- Funzioni di base ----
bool IsCollision(const BoardBits& board, const MinoShape& shape, int x, int y);
int HardDropY(const BoardBits& board, const MinoShape& shape, int x);
int ClearLines(BoardBits& board);
int CalculateDamage(int linesCleared, bool tSpin, bool btb, int combo, bool perfectClear);
void AddGarbage(BoardBits& board, int lines, std::mt19937& rng);

// ---- Enumerazione piazzamenti ----
std::vector<PlacementResult> EnumerateAllPlacements(
    const BoardBits& board, PType pieceType,
    bool canHold, PType holdType,
    int btb, int combo);

// ---- Funzioni giocatore ----
void stepPlayer(PlayerState& ps, double dt, double softDropSpeed);
void hardDropPlayer(PlayerState& ps);
void holdPiece(PlayerState& ps);
void applyMoveRepeat(PlayerState& ps, int dir, double dt, double das, double arr);
void finishLineClear(PlayerState& ps, int cleared, bool tSpin, bool perfectClear);
void lockAndSpawn(PlayerState& ps);