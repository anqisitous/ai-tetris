// ===================================================================
// game_engine.cpp - Implementazione (Immutabile)
// ===================================================================
#include "game_engine.h"
#include <algorithm>
#include <cstring>

const PType ALL_TYPES[7] = {PType::I, PType::O, PType::T, PType::S, PType::Z, PType::J, PType::L};

const SDL_Color COLORS[7] = {
    {0,255,255}, {255,255,0}, {128,0,128}, {0,255,0},
    {255,0,0}, {0,0,255}, {255,165,0}
};

// ---- Forme ----
// rows[r] は上から r 番目の段の列マスク (bit0 = ミノの左端の列)。
// height はそのミノが占める段数。空の段は含めない。
const MinoShape SHAPES[7][4] = {
    // I
    { {{0xF,0,0,0},1}, {{0x4,0x4,0x4,0x4},4}, {{0xF,0,0,0},1}, {{0x2,0x2,0x2,0x2},4} },
    // O
    { {{0x6,0x6,0,0},2}, {{0x6,0x6,0,0},2}, {{0x6,0x6,0,0},2}, {{0x6,0x6,0,0},2} },
    // T
    { {{0x2,0x7,0,0},2}, {{0x2,0x6,0x2,0},3}, {{0x7,0x2,0,0},2}, {{0x2,0x3,0x2,0},3} },
    // S
    { {{0x6,0x3,0,0},2}, {{0x2,0x6,0x4,0},3}, {{0x6,0x3,0,0},2}, {{0x1,0x3,0x2,0},3} },
    // Z
    { {{0x3,0x6,0,0},2}, {{0x4,0x6,0x2,0},3}, {{0x3,0x6,0,0},2}, {{0x2,0x3,0x1,0},3} },
    // J
    { {{0x1,0x7,0,0},2}, {{0x6,0x2,0x2,0},3}, {{0x7,0x4,0,0},2}, {{0x2,0x2,0x3,0},3} },
    // L
    { {{0x4,0x7,0,0},2}, {{0x2,0x2,0x6,0},3}, {{0x7,0x1,0,0},2}, {{0x3,0x2,0x2,0},3} }
};

// ---- Kick tables (semplificate) ----
const int8_t KICK_I[8][5][2] = {
    {{0,0},{-1,0},{2,0},{-1,0},{2,0}},
    {{0,0},{1,0},{-2,0},{1,0},{-2,0}},
    {{0,0},{1,0},{-2,0},{1,0},{-2,0}},
    {{0,0},{-1,0},{2,0},{-1,0},{2,0}},
    {{0,0},{1,0},{-2,0},{1,0},{-2,0}},
    {{0,0},{-1,0},{2,0},{-1,0},{2,0}},
    {{0,0},{-1,0},{2,0},{-1,0},{2,0}},
    {{0,0},{1,0},{-2,0},{1,0},{-2,0}}
};

const int8_t KICK_OTHER[8][5][2] = {
    {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}},
    {{0,0},{1,0},{1,-1},{0,2},{1,2}},
    {{0,0},{1,0},{1,-1},{0,2},{1,2}},
    {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}},
    {{0,0},{1,0},{1,1},{0,-2},{1,-2}},
    {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}},
    {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}},
    {{0,0},{1,0},{1,1},{0,-2},{1,-2}}
};

// ---- Maschera di una riga della mino traslata di x ----
// 盤外にはみ出す場合は false を返す (占有マスが盤面から消えないようにする)
bool ShiftRowMask(uint16_t rowMask, int x, uint16_t& out) {
    if (rowMask == 0) { out = 0; return true; }
    if (x >= BOARD_W || x <= -4) return false;
    if (x >= 0) {
        uint32_t shifted = static_cast<uint32_t>(rowMask) << x;
        if (shifted & ~static_cast<uint32_t>(0x3FF)) return false;
        out = static_cast<uint16_t>(shifted);
    } else {
        int shift = -x;
        if (rowMask & ((1u << shift) - 1)) return false;  // 左にはみ出す
        out = static_cast<uint16_t>(rowMask >> shift);
    }
    return true;
}

// ---- Collisione ----
bool IsCollision(const BoardBits& board, const MinoShape& shape, int x, int y) {
    for (int r = 0; r < shape.height; ++r) {
        uint16_t mask = 0;
        if (!ShiftRowMask(shape.rows[r], x, mask)) return true;
        if (mask == 0) continue;
        int row = y + r;
        if (row < 0) continue;
        if (row >= BOARD_H) return true;
        if (board[row] & mask) return true;
    }
    return false;
}

// ---- Hard Drop Y ----
int HardDropY(const BoardBits& board, const MinoShape& shape, int x) {
    if (IsCollision(board, shape, x, 0)) return -1;
    int y = 0;
    while (!IsCollision(board, shape, x, y + 1)) {
        ++y;
        if (y >= BOARD_H) return -1;
    }
    return y;
}

// ---- Clear Lines ----
int ClearLines(BoardBits& board) {
    int cleared = 0;
    for (int r = BOARD_H - 1; r >= 0; ) {
        if (board[r] == 0x3FF) {
            for (int rr = r; rr > 0; --rr) board[rr] = board[rr - 1];
            board[0] = 0;
            ++cleared;
        } else --r;
    }
    return cleared;
}

// ---- Danno ----
int CalculateDamage(int linesCleared, bool tSpin, bool btb, int combo, bool perfectClear) {
    if (perfectClear) return 10;
    int d = 0;
    if (tSpin) {
        if (linesCleared == 1) d = 2;
        else if (linesCleared == 2) d = 4;
        else if (linesCleared == 3) d = 6;
    } else {
        if (linesCleared == 2) d = 1;
        else if (linesCleared == 3) d = 2;
        else if (linesCleared == 4) d = 4;
    }
    if (btb && d > 0) d += 1;
    if (combo > 0) {
        int ren[] = {0,0,1,2,3,4,4,4,4,4,4,5,5,5};
        if (combo < 14) d += ren[combo];
        else d += 5;
    }
    return d;
}

// ---- Garbage (10% foro spostato) ----
void AddGarbage(BoardBits& board, int lines, std::mt19937& rng) {
    if (lines <= 0) return;
    std::uniform_int_distribution<int> colDist(0, 9);
    std::uniform_real_distribution<double> probDist(0.0, 1.0);
    int holeCol = colDist(rng);
    
    for (int i = 0; i < lines; ++i) {
        int actualHole = holeCol;
        if (probDist(rng) < 0.10) actualHole = colDist(rng);
        
        for (int r = BOARD_BUFFER - 1; r > 0; --r) board[r] = board[r - 1];
        board[0] = 0;
        
        uint16_t garbageRow = 0x3FF ^ (1 << actualHole);
        for (int r = BOARD_H - 1; r >= 0; --r) {
            if (board[r] == 0) { board[r] = garbageRow; break; }
        }
    }
}

// ---- Enumerazione piazzamenti (BFS con soft drop e kick sequenziale) ----
std::vector<PlacementResult> EnumerateAllPlacements(
    const BoardBits& board, PType pieceType,
    bool canHold, PType holdType, int btb, int combo)
{
    std::vector<PlacementResult> results;
    std::vector<std::pair<PType, bool>> pieces = {{pieceType, false}};
    if (canHold && holdType != pieceType) pieces.push_back({holdType, true});

    for (auto [ptype, usedHold] : pieces) {
        for (int rot = 0; rot < 4; ++rot) {
            const MinoShape& shape = SHAPES[(int)ptype][rot];
            
            // Lock candidates
            std::vector<PlacementState> lockCandidates;
            for (int x = -2; x < BOARD_W + 2; ++x) {
                if (IsCollision(board, shape, x, 0)) continue;
                int y = HardDropY(board, shape, x);
                if (y >= 0) lockCandidates.push_back({x, y, rot});
            }
            
            // BFS da spawn
            int spawnX = 3, spawnY = 0;
            if (IsCollision(board, shape, spawnX, spawnY)) continue;
            
            std::unordered_set<PlacementState, PlacementStateHash> reachable;
            std::queue<PlacementState> q;
            std::unordered_map<PlacementState, int, PlacementStateHash> dist;
            reachable.insert({spawnX, spawnY, rot});
            q.push({spawnX, spawnY, rot});
            dist[{spawnX, spawnY, rot}] = 0;
            
            while (!q.empty()) {
                PlacementState cur = q.front(); q.pop();
                int curDist = dist[cur];
                
                // Soft drop
                if (!IsCollision(board, shape, cur.x, cur.y + 1)) {
                    PlacementState nxt = {cur.x, cur.y + 1, cur.rot};
                    if (!reachable.count(nxt)) {
                        reachable.insert(nxt);
                        dist[nxt] = curDist + 1;
                        q.push(nxt);
                    }
                }
                // Sinistra
                if (!IsCollision(board, shape, cur.x - 1, cur.y)) {
                    PlacementState nxt = {cur.x - 1, cur.y, cur.rot};
                    if (!reachable.count(nxt)) {
                        reachable.insert(nxt);
                        dist[nxt] = curDist + 1;
                        q.push(nxt);
                    }
                }
                // Destra
                if (!IsCollision(board, shape, cur.x + 1, cur.y)) {
                    PlacementState nxt = {cur.x + 1, cur.y, cur.rot};
                    if (!reachable.count(nxt)) {
                        reachable.insert(nxt);
                        dist[nxt] = curDist + 1;
                        q.push(nxt);
                    }
                }
                // Rotazioni con kick sequenziale
                for (int dir : {1, 3}) {
                    int newRot = (cur.rot + dir) % 4;
                    const MinoShape& newShape = SHAPES[(int)ptype][newRot];
                    const int8_t (*tests)[5][2] = (ptype == PType::I) ? KICK_I : KICK_OTHER;
                    int tableIdx = -1;
                    if (ptype == PType::I) {
                        if (cur.rot == 0 && newRot == 1) tableIdx = 0;
                        else if (cur.rot == 1 && newRot == 0) tableIdx = 1;
                        else if (cur.rot == 1 && newRot == 2) tableIdx = 2;
                        else if (cur.rot == 2 && newRot == 1) tableIdx = 3;
                        else if (cur.rot == 2 && newRot == 3) tableIdx = 4;
                        else if (cur.rot == 3 && newRot == 2) tableIdx = 5;
                        else if (cur.rot == 3 && newRot == 0) tableIdx = 6;
                        else if (cur.rot == 0 && newRot == 3) tableIdx = 7;
                    } else {
                        if (cur.rot == 0 && newRot == 1) tableIdx = 0;
                        else if (cur.rot == 1 && newRot == 0) tableIdx = 1;
                        else if (cur.rot == 1 && newRot == 2) tableIdx = 2;
                        else if (cur.rot == 2 && newRot == 1) tableIdx = 3;
                        else if (cur.rot == 2 && newRot == 3) tableIdx = 4;
                        else if (cur.rot == 3 && newRot == 2) tableIdx = 5;
                        else if (cur.rot == 3 && newRot == 0) tableIdx = 6;
                        else if (cur.rot == 0 && newRot == 3) tableIdx = 7;
                    }
                    if (tableIdx >= 0) {
                        for (int k = 0; k < 5; ++k) {
                            int tx = cur.x + tests[tableIdx][k][0];
                            int ty = cur.y + tests[tableIdx][k][1];
                            if (!IsCollision(board, newShape, tx, ty)) {
                                PlacementState nxt = {tx, ty, newRot};
                                if (!reachable.count(nxt)) {
                                    reachable.insert(nxt);
                                    dist[nxt] = curDist + 1;
                                    q.push(nxt);
                                }
                                break;  // Kick sequenziale!
                            }
                        }
                    }
                }
            }
            
            // Genera risultati per lock candidates raggiungibili
            for (auto& lock : lockCandidates) {
                if (!reachable.count(lock)) continue;
                
                BoardBits newBoard;
                std::memcpy(&newBoard, &board, sizeof(BoardBits));
                for (int r = 0; r < shape.height; ++r) {
                    int row = lock.y + r;
                    if (row < 0 || row >= BOARD_BUFFER) continue;
                    uint16_t mask = shape.rows[r];
                    if (lock.x >= 0) mask <<= lock.x;
                    else mask >>= (-lock.x);
                    newBoard[row] |= mask;
                }
                int cleared = ClearLines(newBoard);
                bool tSpin = false;
                int damage = CalculateDamage(cleared, tSpin, btb, combo, false);
                results.push_back({newBoard, damage, tSpin, cleared, lock.x, lock.y, lock.rot, usedHold, dist[lock]});
            }
        }
    }
    return results;
}

// ---- Inizializzazione giocatore ----
void PlayerState::init(int seed) {
    std::mt19937 rng(seed);
    board.fill(0);
    bag.clear();
    next.clear();
    for (int i = 0; i < 7; ++i) bag.push_back(ALL_TYPES[i]);
    std::shuffle(bag.begin(), bag.end(), rng);
    while (next.size() < 5) {
        if (bag.empty()) {
            for (int i = 0; i < 7; ++i) bag.push_back(ALL_TYPES[i]);
            std::shuffle(bag.begin(), bag.end(), rng);
        }
        next.push_back(bag.front());
        bag.pop_front();
    }
    curType = next.front(); next.pop_front();
    while (next.size() < 5) {
        if (bag.empty()) {
            for (int i = 0; i < 7; ++i) bag.push_back(ALL_TYPES[i]);
            std::shuffle(bag.begin(), bag.end(), rng);
        }
        next.push_back(bag.front());
        bag.pop_front();
    }
    hold = PType::I;
    canHold = true;
    holdUsed = false;
    combo = 0;
    btb = 0;
    gameOver = false;
    curX = 3; curY = 0; curRot = 0;
    linesCleared = 0;
    damageBuff = 0;
    score = 0;
    level = 1;
}

PType PlayerState::popNext() {
    PType t = next.front();
    next.pop_front();
    if (bag.empty()) {
        for (int i = 0; i < 7; ++i) bag.push_back(ALL_TYPES[i]);
        std::shuffle(bag.begin(), bag.end(), std::mt19937(std::random_device{}()));
    }
    next.push_back(bag.front());
    bag.pop_front();
    return t;
}

// ---- Step giocatore ----
void stepPlayer(PlayerState& ps, double dt, double softDropSpeed) {
    if (ps.gameOver) return;
    double normalInterval = std::max(0.05, 0.6 - ps.level * 0.04);
    double dropInterval = ps.softDrop ? std::min(normalInterval, softDropSpeed) : normalInterval;
    ps.tickAcc += dt;
    if (ps.tickAcc >= dropInterval) {
        ps.tickAcc = 0;
        const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
        if (!IsCollision(ps.board, shape, ps.curX, ps.curY + 1)) {
            ps.curY++;
        } else {
            lockAndSpawn(ps);
        }
    }
}

void hardDropPlayer(PlayerState& ps) {
    if (ps.gameOver) return;
    const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
    ps.curY = HardDropY(ps.board, shape, ps.curX);
    lockAndSpawn(ps);
}

void holdPiece(PlayerState& ps) {
    if (!ps.canHold || ps.gameOver) return;
    if (!ps.holdUsed) {
        ps.hold = ps.curType;
        ps.holdUsed = true;
        ps.curType = ps.popNext();
    } else {
        std::swap(ps.hold, ps.curType);
    }
    ps.curX = 3; ps.curY = 0; ps.curRot = 0;
    ps.canHold = false;
}

void applyMoveRepeat(PlayerState& ps, int dir, double dt, double das, double arr) {
    if (dir == 0) { ps.moveTimer = 0; ps.moveDir = 0; return; }
    if (ps.moveDir != dir) {
        ps.moveDir = dir;
        ps.moveTimer = 0;
        const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
        if (!IsCollision(ps.board, shape, ps.curX + dir, ps.curY)) ps.curX += dir;
        return;
    }
    ps.moveTimer += dt;
    while (ps.moveTimer >= (ps.moveTimer >= das ? arr : das)) {
        ps.moveTimer -= (ps.moveTimer >= das ? arr : das);
        const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
        if (!IsCollision(ps.board, shape, ps.curX + dir, ps.curY)) ps.curX += dir;
    }
}

void finishLineClear(PlayerState& ps, int cleared, bool tSpin, bool perfectClear) {
    if (cleared > 0) ++ps.combo; else ps.combo = 0;
    int damage = CalculateDamage(cleared, tSpin, ps.btb, ps.combo, perfectClear);
    damage += ps.damageBuff;
    ps.damageBuff = 0;
    bool btbEligible = (tSpin && cleared > 0) || (!tSpin && cleared == 4);
    if (btbEligible) ps.btb = true; else if (cleared > 0) ps.btb = false;
    ps.linesCleared = damage;
    ps.score += damage * 10;
    ps.level = 1 + ps.score / 100;
    ps.curType = ps.popNext();
    ps.curX = 3; ps.curY = 0; ps.curRot = 0;
    ps.canHold = true;
}

void lockAndSpawn(PlayerState& ps) {
    BoardBits& board = ps.board;
    const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
    for (int r = 0; r < shape.height; ++r) {
        int row = ps.curY + r;
        if (row < 0 || row >= BOARD_BUFFER) continue;
        uint16_t mask = shape.rows[r];
        if (ps.curX >= 0) mask <<= ps.curX;
        else mask >>= (-ps.curX);
        board[row] |= mask;
    }
    int cleared = ClearLines(board);
    if (cleared == 0) {
        finishLineClear(ps, 0, false, false);
    } else {
        bool perfectClear = true;
        for (int r = 0; r < BOARD_H && perfectClear; ++r)
            if (board[r] != 0) perfectClear = false;
        finishLineClear(ps, cleared, false, perfectClear);
    }
}