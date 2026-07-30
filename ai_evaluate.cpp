// ===================================================================
// ai_evaluate.cpp - Implementazione della valutazione
// ===================================================================
#include "ai_evaluate.h"
#include <algorithm>
#include <cmath>

// ---- Costanti per gruppi di colonne 1-2-4-2-1 ----
constexpr int COL_GROUP[10] = {0, 1, 1, 2, 2, 2, 2, 3, 3, 4};

// ---- Altezza di una colonna ----
int getColumnHeight(const BoardBits& board, int col) {
    for (int r = BOARD_H - 1; r >= 0; --r) {
        if (board[r] & (1 << col)) return r + 1;
    }
    return 0;
}

// ---- Altezza media di un gruppo ----
float getGroupHeight(const BoardBits& board, int groupIdx) {
    float sum = 0;
    int count = 0;
    for (int c = 0; c < BOARD_W; ++c) {
        if (COL_GROUP[c] == groupIdx) {
            sum += getColumnHeight(board, c);
            count++;
        }
    }
    return count > 0 ? sum / count : 0;
}

// ---- Altezze minime dei 5 gruppi ----
std::array<int, 5> getGroupMinHeights(const BoardBits& board) {
    std::array<int, 5> mins;
    mins[0] = getColumnHeight(board, 0);
    mins[1] = std::min(getColumnHeight(board, 1), getColumnHeight(board, 2));
    mins[2] = std::min({getColumnHeight(board, 3), getColumnHeight(board, 4),
                        getColumnHeight(board, 5), getColumnHeight(board, 6)});
    mins[3] = std::min(getColumnHeight(board, 7), getColumnHeight(board, 8));
    mins[4] = getColumnHeight(board, 9);
    return mins;
}

// ---- Cella piena? ----
bool isFilled(const BoardBits& board, int col, int row) {
    if (row < 0 || row >= BOARD_H || col < 0 || col >= BOARD_W) return false;
    return board[row] & (1 << col);
}

// ---- T-Spin detection ----
bool isTSpin(const BoardBits& board, int x, int y, int rot) {
    // Tの中心は (x+1, y+1)
    int cx = x + 1;
    int cy = y + 1;
    
    // 4隅をチェック
    int corners = 0;
    if (isFilled(board, cx - 1, cy - 1)) corners++;
    if (isFilled(board, cx + 1, cy - 1)) corners++;
    if (isFilled(board, cx - 1, cy + 1)) corners++;
    if (isFilled(board, cx + 1, cy + 1)) corners++;
    
    return corners >= 3;
}

// ---- Conta quanti TSD sono possibili ----
int countTSDDoubleSetups(const BoardBits& board) {
    int count = 0;
    
    for (int rot = 0; rot < 4; ++rot) {
        const MinoShape& shape = SHAPES[(int)PType::T][rot];
        
        for (int x = -2; x < BOARD_W + 2; ++x) {
            if (IsCollision(board, shape, x, 0)) continue;
            int y = HardDropY(board, shape, x);
            if (y < 0) continue;
            
            // T-Spin?
            if (!isTSpin(board, x, y, rot)) continue;
            
            // Simula il piazzamento e conta linee
            BoardBits temp = board;
            for (int r = 0; r < shape.height; ++r) {
                int row = y + r;
                if (row < 0 || row >= BOARD_H) continue;
                uint16_t mask = shape.rows[r];
                if (x >= 0) mask <<= x;
                else mask >>= (-x);
                temp[row] |= mask;
            }
            int cleared = ClearLines(temp);
            if (cleared == 2) count++;  // TSD = 2 linee
        }
    }
    return count;
}

// ---- Double Dagger Destro ----
bool isDoubleDaggerRight(const BoardBits& board) {
    int h7 = getColumnHeight(board, 7);
    int h8 = getColumnHeight(board, 8);
    int h9 = getColumnHeight(board, 9);
    int H = std::max({h7, h8, h9});
    
    // Top: 3 blocchi alla stessa altezza
    if (h7 != H || h8 != H || h9 != H) return false;
    
    // TSD+ pattern: H-1 ha 7 e 9 pieni, 8 vuoto (forma a T)
    if (!isFilled(board, 7, H-1) || isFilled(board, 8, H-1) || !isFilled(board, 9, H-1))
        return false;
    
    // TSD- setup: H-3 ha tutti pieni, H-2 ha 1 buco
    if (H - 3 < 0) return false;
    if (!isFilled(board, 7, H-3) || !isFilled(board, 8, H-3) || !isFilled(board, 9, H-3))
        return false;
    
    int filledH2 = isFilled(board, 7, H-2) + isFilled(board, 8, H-2) + isFilled(board, 9, H-2);
    return filledH2 == 2;  // Esattamente 1 buco
}

// ---- Double Dagger Sinistro ----
bool isDoubleDaggerLeft(const BoardBits& board) {
    int h0 = getColumnHeight(board, 0);
    int h1 = getColumnHeight(board, 1);
    int h2 = getColumnHeight(board, 2);
    int H = std::max({h0, h1, h2});
    
    if (h0 != H || h1 != H || h2 != H) return false;
    
    if (!isFilled(board, 0, H-1) || isFilled(board, 1, H-1) || !isFilled(board, 2, H-1))
        return false;
    
    if (H - 3 < 0) return false;
    if (!isFilled(board, 0, H-3) || !isFilled(board, 1, H-3) || !isFilled(board, 2, H-3))
        return false;
    
    int filledH2 = isFilled(board, 0, H-2) + isFilled(board, 1, H-2) + isFilled(board, 2, H-2);
    return filledH2 == 2;
}

// ---- Double Dagger (entrambi i lati) ----
bool isDoubleDagger(const BoardBits& board) {
    return isDoubleDaggerRight(board) || isDoubleDaggerLeft(board);
}

// ---- Conta buchi ----
int countHoles(const BoardBits& board) {
    int holes = 0;
    for (int c = 0; c < BOARD_W; ++c) {
        bool foundBlock = false;
        for (int r = BOARD_H - 1; r >= 0; --r) {
            if (board[r] & (1 << c)) {
                foundBlock = true;
            } else if (foundBlock) {
                holes++;
            }
        }
    }
    return holes;
}

// ---- Qualità generale del terreno ----
float evaluateTerrainQuality(const BoardBits& board) {
    float score = 0.0f;
    
    // Penalità buchi
    score -= countHoles(board) * 15.0f;
    
    // Penalità altezza media
    float avgH = 0;
    for (int c = 0; c < BOARD_W; ++c) avgH += getColumnHeight(board, c);
    avgH /= BOARD_W;
    score -= avgH * 5.0f;
    
    // Premio Double Dagger
    if (isDoubleDagger(board)) score += 50.0f;
    
    // Premio TSD setups
    score += countTSDDoubleSetups(board) * 20.0f;
    
    return score;
}

// ---- Prontezza Double Dagger ----
float evaluateDoubleDaggerReadiness(const BoardBits& board) {
    float score = 0.0f;
    
    score -= countHoles(board) * 10.0f;
    
    if (isDoubleDaggerRight(board)) score += 50.0f;
    if (isDoubleDaggerLeft(board)) score += 50.0f;
    
    // Premio colonne uniformi
    int h7 = getColumnHeight(board, 7), h8 = getColumnHeight(board, 8), h9 = getColumnHeight(board, 9);
    if (h7 == h8 && h8 == h9) score += 20.0f;
    
    int h0 = getColumnHeight(board, 0), h1 = getColumnHeight(board, 1), h2 = getColumnHeight(board, 2);
    if (h0 == h1 && h1 == h2) score += 20.0f;
    
    return score;
}

// ---- Spettro di parità (4n+2) ----
float calculateParitySpectrum(const BoardBits& board) {
    float spectrum = 0;
    for (int r = 0; r < 20; ++r) {
        int filled = 0;
        for (int c = 0; c < BOARD_W; ++c) {
            if (board[r] & (1 << c)) filled++;
        }
        int mod4 = filled % 4;
        if (mod4 == 2) spectrum += 1.0f;
        else if (mod4 == 1 || mod4 == 3) spectrum += 0.5f;
    }
    return spectrum;
}

// ---- Centro aperto ----
bool isCenterOpen(const BoardBits& board) {
    float g1 = getGroupHeight(board, 1);
    float g2 = getGroupHeight(board, 2);
    float g3 = getGroupHeight(board, 3);
    return (g1 >= 4.0f || g3 >= 4.0f) && g2 < std::min(g1, g3);
}

// ---- Estrai aspetto ----
Aspect extractAspect(const BoardBits& board, float timingDiff, int combo,
                     const std::deque<PType>& next) {
    Aspect a;
    a.snapshot = board;
    auto& v = a.values;
    
    v.push_back(getColumnHeight(board, 0));
    float mid = 0;
    for (int c = 3; c <= 6; ++c) mid += getColumnHeight(board, c);
    v.push_back(mid / 4.0f);
    
    for (int r = 0; r < 20; ++r) {
        int empty = 0;
        for (int c = 0; c < BOARD_W; ++c) if (!(board[r] & (1 << c))) empty++;
        v.push_back(empty == 1 ? 1.0f : 0.0f);
    }
    
    auto heights = getGroupMinHeights(board);
    for (int h : heights) v.push_back(h);
    
    v.push_back(isDoubleDagger(board) ? 1.0f : 0.0f);
    v.push_back(timingDiff);
    v.push_back(countHoles(board));
    v.push_back(calculateParitySpectrum(board));
    v.push_back(isCenterOpen(board) ? 1.0f : 0.0f);
    v.push_back(countTSDDoubleSetups(board));
    v.push_back(combo > 0 ? 1.0f : 0.0f);
    
    return a;
}

// ---- Distanza tra aspetti ----
float aspectDistance(const Aspect& a, const Aspect& b) {
    float sum = 0;
    size_t n = std::min(a.values.size(), b.values.size());
    for (size_t i = 0; i < n; ++i) {
        float diff = a.values[i] - b.values[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

// ---- Top 3 righe ----
std::bitset<30> GetTop3Rows(const BoardBits& board) {
    std::bitset<30> bits;
    for (int r = BOARD_H - 3; r < BOARD_H; ++r) {
        int localRow = r - (BOARD_H - 3);
        for (int c = 0; c < BOARD_W; ++c) {
            if (board[r] & (1 << c)) bits.set(localRow * BOARD_W + c);
        }
    }
    return bits;
}

// ---- Finestra di righe ----
std::bitset<30> GetRows(const BoardBits& board, int startRow, int endRow) {
    std::bitset<30> bits;
    for (int r = startRow; r < endRow && r < BOARD_H; ++r) {
        int localRow = r - startRow;
        for (int c = 0; c < BOARD_W; ++c) {
            if (board[r] & (1 << c)) bits.set(localRow * BOARD_W + c);
        }
    }
    return bits;
}

// ---- LSH hash ----
uint64_t lshHash(const std::vector<float>& vec, int bits) {
    static std::vector<std::vector<float>> randomVectors;
    static bool initialized = false;
    if (!initialized) {
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        randomVectors.resize(bits);
        for (int b = 0; b < bits; ++b) {
            randomVectors[b].resize(2);
            randomVectors[b][0] = dist(rng);
            randomVectors[b][1] = dist(rng);
        }
        initialized = true;
    }
    
    uint64_t hash = 0;
    float limitedVec[2] = {vec[0], vec[25]};
    for (int b = 0; b < bits; ++b) {
        float dot = limitedVec[0] * randomVectors[b][0] + 
                    limitedVec[1] * randomVectors[b][1];
        if (dot > 0.0f) hash |= (1ULL << b);
    }
    return hash;
}