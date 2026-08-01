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
const MinoShape SHAPES[7][4] = {
    { {{0x0F00,0,0,0},1}, {{0x2222,0,0,0},4}, {{0x0F00,0,0,0},1}, {{0x2222,0,0,0},4} },
    { {{0x6600,0,0,0},2}, {{0x6600,0,0,0},2}, {{0x6600,0,0,0},2}, {{0x6600,0,0,0},2} },
    { {{0x2700,0,0,0},2}, {{0x2320,0,0,0},3}, {{0x0E40,0,0,0},2}, {{0x2620,0,0,0},3} },
    { {{0x3600,0,0,0},2}, {{0x2310,0,0,0},3}, {{0x3600,0,0,0},2}, {{0x2310,0,0,0},3} },
    { {{0x6300,0,0,0},2}, {{0x1320,0,0,0},3}, {{0x6300,0,0,0},2}, {{0x1320,0,0,0},3} },
    { {{0x4700,0,0,0},2}, {{0x3220,0,0,0},3}, {{0x0E20,0,0,0},2}, {{0x2260,0,0,0},3} },
    { {{0x1700,0,0,0},2}, {{0x2230,0,0,0},3}, {{0x0E80,0,0,0},2}, {{0x6220,0,0,0},3} }
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

// ---- Collisione ----
bool IsCollision(const BoardBits& board, const MinoShape& shape, int x, int y) {
    for (int r = 0; r < shape.height; ++r) {
        int row = y + r;
        if (row < 0) continue;
        if (row >= BOARD_BUFFER) return true;
        uint16_t mask = shape.rows[r];
        if (x >= 0) mask <<= x;
        else mask >>= (-x);
        if (mask & 0xFC00) return true;
        if (board[row] & mask) return true;
    }
    return false;
}

// ---- Hard Drop Y ----
int HardDropY(const BoardBits& board, const MinoShape& shape, int x) {
    int y = 0;
    while (!IsCollision(board, shape, x, y + 1)) {
        ++y;
        if (y >= BOARD_BUFFER) return -1;
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