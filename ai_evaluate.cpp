// ===================================================================
// ai_evaluate.cpp - Implementazione della valutazione
// ===================================================================
#include "ai_evaluate.h"
#include <algorithm>
#include <cmath>
#include <numeric>

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
    
    // Horizontal parity penalty for perfect clear impossibility
    int hParity = calculateHorizontalParity(board);
    // If horizontal parity is 1 or 3, perfect clear is impossible
    if (hParity % 4 == 1 || hParity % 4 == 3) {
        score -= 500.0f;  // Strong penalty for impossible perfect clear
    }
    
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

// ---- Horizontal Parity (横パリティ) ----
// 各列のブロック数が奇数か偶数かをカウント
// 横パリティ = 奇数の列の数
int calculateHorizontalParity(const BoardBits& board) {
    int oddColumns = 0;
    for (int c = 0; c < BOARD_W; ++c) {
        int count = 0;
        for (int r = 0; r < BOARD_H; ++r) {
            if (board[r] & (1 << c)) count++;
        }
        if (count % 2 != 0) oddColumns++;
    }
    return oddColumns;
}

// ---- Perfect Clear Theorem (パフェ定理) ----
// 定理: [JLTが0か180度の個数] = O個数 + [IZS*{±90度どちらか}の個数] + 2n + 現在の横パリティ
// This must hold modulo 2 for perfect clear to be possible
bool isPerfectClearTheoremSatisfied(int jlt_0_180_count, int o_count, int izs_pm90_count, int horizontal_parity) {
    // The theorem states: jlt_0_180 = o + izs_pm90 + 2n + hParity
    // Modulo 2: jlt_0_180 % 2 == (o + izs_pm90 + hParity) % 2
    // Since 2n % 2 = 0, we can ignore it
    int leftSide = jlt_0_180_count % 2;
    int rightSide = (o_count + izs_pm90_count + horizontal_parity) % 2;
    return leftSide == rightSide;
}

// ---- Evaluate Perfect Clear Possibility using Theorem ----
// Returns a score based on how close we are to satisfying the perfect clear theorem
// Higher score means better chance for perfect clear
float evaluatePerfectClearPossibility(const BoardBits& board, 
                                       int jlt_0_180_count, int o_count, int izs_pm90_count) {
    int hParity = calculateHorizontalParity(board);
    
    if (isPerfectClearTheoremSatisfied(jlt_0_180_count, o_count, izs_pm90_count, hParity)) {
        return 100.0f;  // Perfect clear is possible
    } else {
        return -100.0f;  // Perfect clear is impossible
    }
}

// ===================================================================
// Spin Detection Polymorphism (T-Spin + Tetris)
// ===================================================================

// ---- StandardSpinDetector Implementation ----
SpinType StandardSpinDetector::detect(const BoardBits& board, const MinoShape& shape, int x, int y, int rot) const {
    // 1. Check if this is a T piece
    bool isTPiece = false;
    for (int t = 0; t < 7; t++) {
        if (&SHAPES[t][0] == &shape) {
            isTPiece = (t == (int)PType::T);
            break;
        }
    }
    
    // 2. For T piece: Check T-Spin
    if (isTPiece) {
        if (!isTSpin(board, x, y, rot)) {
            return SpinType::NONE;
        }
        
        // Simulate placement
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
        
        // Determine T-Spin type
        if (cleared == 0) return SpinType::T_MINI;
        if (cleared == 1) return SpinType::T_SINGLE;
        if (cleared == 2) return SpinType::T_DOUBLE;
        if (cleared == 3) return SpinType::T_TRIPLE;
        if (cleared == 4) return SpinType::T_TETRIS;
        
        return SpinType::NONE;
    }
    
    // 3. For non-T pieces: Check if it results in Tetris (4 lines)
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
    if (cleared == 4) return SpinType::TETRIS;
    
    return SpinType::NONE;
}

float StandardSpinDetector::getScore(SpinType type, bool isBTB) const {
    float score = 0.0f;
    
    switch (type) {
        case SpinType::T_MINI:   score = 10.0f; break;
        case SpinType::T_SINGLE: score = 30.0f; break;
        case SpinType::T_DOUBLE: score = 60.0f; break;
        case SpinType::T_TRIPLE: score = 100.0f; break;
        case SpinType::T_TETRIS: score = 150.0f; break;  // T-Spin Tetris
        case SpinType::TETRIS:  score = 120.0f; break;  // Regular Tetris
        default: return 0.0f;
    }
    
    // BTB (Back-to-Back) bonus
    if (isBTB) {
        score *= 1.5f;
    }
    
    return score;
}

// ---- SpinEvaluator Implementation ----
SpinEvaluator::SpinEvaluator(bool btb) 
    : detector(std::make_unique<StandardSpinDetector>()), considerBTB(btb) {}

SpinType SpinEvaluator::getSpinType(const BoardBits& board, PType pieceType, int x, int y, int rot) const {
    const MinoShape& shape = SHAPES[(int)pieceType][rot];
    return detector->detect(board, shape, x, y, rot);
}

float SpinEvaluator::evaluate(const BoardBits& board, const std::deque<PType>& next) {
    float score = 0.0f;
    
    // 1. Evaluate all possible T-Spin positions for current board
    for (int rot = 0; rot < 4; ++rot) {
        const MinoShape& shape = SHAPES[(int)PType::T][rot];
        
        for (int x = -2; x < BOARD_W + 2; ++x) {
            if (IsCollision(board, shape, x, 0)) continue;
            int y = HardDropY(board, shape, x);
            if (y < 0) continue;
            
            SpinType type = detector->detect(board, shape, x, y, rot);
            score += detector->getScore(type, considerBTB);
        }
    }
    
    // 2. Bonus if T piece is coming soon in the next queue
    for (int i = 0; i < std::min(3, (int)next.size()); ++i) {
        if (next[i] == PType::T) {
            score *= 1.3f;  // 10% bonus for each T in next 3 pieces
            break;
        }
    }
    
    // 3. Evaluate Tetris (4-line clear) possibilities for all pieces
    for (int pt = 0; pt < 7; ++pt) {
        PType pieceType = (PType)pt;
        for (int rot = 0; rot < 4; ++rot) {
            const MinoShape& shape = SHAPES[pt][rot];
            
            for (int x = -2; x < BOARD_W + 2; ++x) {
                if (IsCollision(board, shape, x, 0)) continue;
                int y = HardDropY(board, shape, x);
                if (y < 0) continue;
                
                SpinType type = detector->detect(board, shape, x, y, rot);
                if (type == SpinType::TETRIS) {
                    score += 80.0f;  // Bonus for Tetris possibility
                }
            }
        }
    }
    
    return score;
}

void SpinEvaluator::setBTB(bool btb) {
    considerBTB = btb;
}

// ===================================================================
// Reachable Space Analysis
// ===================================================================

// ---- 到達可能な空マスをBFSで探索 ----
std::vector<std::vector<bool>> findReachableSpaces(const BoardBits& board) {
    std::vector<std::vector<bool>> reachable(BOARD_H, std::vector<bool>(BOARD_W, false));
    std::queue<std::pair<int, int>> q;

    // 一番上の行（row=0）の空マスから開始
    for (int c = 0; c < BOARD_W; ++c) {
        if (!(board[0] & (1 << c))) {
            reachable[0][c] = true;
            q.push({0, c});
        }
    }

    // BFSで到達可能な空マスを探索
    const int dx[] = {0, 0, -1, 1};
    const int dy[] = {-1, 1, 0, 0};

    while (!q.empty()) {
        auto [r, c] = q.front();
        q.pop();

        for (int i = 0; i < 4; ++i) {
            int nr = r + dy[i];
            int nc = c + dx[i];

            if (nr < 0 || nr >= BOARD_H || nc < 0 || nc >= BOARD_W) continue;
            if (reachable[nr][nc]) continue;
            if (board[nr] & (1 << nc)) continue;  // ブロックがある

            reachable[nr][nc] = true;
            q.push({nr, nc});
        }
    }

    return reachable;
}

// ---- 連結成分を抽出 ----
std::vector<ConnectedComponent> findConnectedComponents(const std::vector<std::vector<bool>>& reachable) {
    std::vector<std::vector<bool>> visited(BOARD_H, std::vector<bool>(BOARD_W, false));
    std::vector<ConnectedComponent> components;

    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            if (reachable[r][c] && !visited[r][c]) {
                // 新しい連結成分を発見
                ConnectedComponent comp;
                comp.left = c;
                comp.right = c;
                comp.top = r;
                comp.bottom = r;

                std::queue<std::pair<int, int>> q;
                q.push({r, c});
                visited[r][c] = true;

                while (!q.empty()) {
                    auto [cr, cc] = q.front();
                    q.pop();
                    comp.cells.push_back({cr, cc});

                    // 境界を更新
                    comp.left = std::min(comp.left, cc);
                    comp.right = std::max(comp.right, cc);
                    comp.top = std::min(comp.top, cr);
                    comp.bottom = std::max(comp.bottom, cr);

                    // 4方向に探索
                    const int dx[] = {0, 0, -1, 1};
                    const int dy[] = {-1, 1, 0, 0};

                    for (int i = 0; i < 4; ++i) {
                        int nr = cr + dy[i];
                        int nc = cc + dx[i];

                        if (nr < 0 || nr >= BOARD_H || nc < 0 || nc >= BOARD_W) continue;
                        if (!reachable[nr][nc] || visited[nr][nc]) continue;

                        visited[nr][nc] = true;
                        q.push({nr, nc});
                    }
                }

                comp.width = comp.right - comp.left + 1;
                comp.height = comp.bottom - comp.top + 1;
                components.push_back(comp);
            }
        }
    }

    return components;
}

// ---- 到達可能空間を解析 ----
ReachableSpaceInfo analyzeReachableSpaces(const BoardBits& board) {
    auto reachable = findReachableSpaces(board);
    auto components = findConnectedComponents(reachable);

    ReachableSpaceInfo info;
    info.components = components;

    // テトリスの穴とその他の穴を分類
    for (const auto& comp : components) {
        // 列ごとのセル情報を構築
        std::unordered_map<int, std::pair<int, int>> colRange;
        std::unordered_map<int, std::unordered_set<int>> colCells;
        
        for (const auto& [r, c] : comp.cells) {
            if (colRange.find(c) == colRange.end()) {
                colRange[c] = {r, r};
            } else {
                colRange[c].first = std::min(colRange[c].first, r);
                colRange[c].second = std::max(colRange[c].second, r);
            }
            colCells[c].insert(r);
        }
        
        // 各列で連続性をチェックし、最大の連続深さを求める
        int maxContinuousDepth = 0;
        for (const auto& [c, range] : colRange) {
            int minR = range.first;
            int maxR = range.second;
            bool isContinuous = true;
            
            for (int r = minR; r <= maxR; ++r) {
                if (colCells[c].find(r) == colCells[c].end()) {
                    isContinuous = false;
                    break;
                }
            }
            
            if (isContinuous) {
                maxContinuousDepth = std::max(maxContinuousDepth, maxR - minR + 1);
            }
        }
        
        // テトリスの穴かどうかを判定
        if (comp.width == 1 && maxContinuousDepth >= EvalWeights::WELL_MIN_DEPTH) {
            info.tetrisWells.push_back(comp);
        } else if (comp.height >= 1) {  // 穴（高さ1以上）
            info.otherHoles.push_back(comp);
        }
    }

    return info;
}

// ---- 一番目に低い開いている穴を特定 ----
ConnectedComponent getLowestReachableHole(const std::vector<ConnectedComponent>& holes) {
    if (holes.empty()) {
        return ConnectedComponent{0, 0, 0, 0, 0, 0, {}};
    }

    // 一番上（topが最小）の穴を返す
    auto lowest = *std::min_element(holes.begin(), holes.end(),
        [](const ConnectedComponent& a, const ConnectedComponent& b) {
            return a.top < b.top;
        });

    return lowest;
}

// ---- 穴を評価 ----
HoleEvaluation evaluateHole(const ConnectedComponent& comp, const BoardBits& board) {
    HoleEvaluation eval;

    // 面積
    eval.area = static_cast<float>(comp.cells.size());

    // 最大深さ（一番下の行 - 一番上の行 + 1）
    eval.maxDepth = static_cast<float>(comp.bottom - comp.top + 1);

    // 上に覆われているセル数（穴の上にブロックがあるか）
    eval.coveredCells = 0.0f;
    for (const auto& [r, c] : comp.cells) {
        if (r > 0 && (board[r - 1] & (1 << c))) {
            eval.coveredCells += 1.0f;
        }
    }

    // 形状ペナルティ（幅が広いほど埋めにくい）
    if (comp.width >= 3) {
        eval.shapePenalty = static_cast<float>(comp.width - 2) * EvalWeights::HOLE_SHAPE_BASE;
    } else {
        eval.shapePenalty = 0.0f;
    }

    // 総評価スコア
    eval.totalScore = eval.area * EvalWeights::HOLE_AREA
                    + eval.maxDepth * EvalWeights::HOLE_DEPTH
                    + eval.coveredCells * EvalWeights::HOLE_COVERED
                    + eval.shapePenalty;

    return eval;
}

// ---- テトリスの穴（Well）の評価 ----
TetrisWellEvaluation evaluateTetrisWell(const ConnectedComponent& comp, const BoardBits& board) {
    TetrisWellEvaluation eval;

    // 列ごとのセル情報を構築
    std::unordered_map<int, std::pair<int, int>> colRange;
    std::unordered_map<int, std::unordered_set<int>> colCells;
    
    for (const auto& [r, c] : comp.cells) {
        if (colRange.find(c) == colRange.end()) {
            colRange[c] = {r, r};
        } else {
            colRange[c].first = std::min(colRange[c].first, r);
            colRange[c].second = std::max(colRange[c].second, r);
        }
        colCells[c].insert(r);
    }
    
    // 各列で連続性をチェックし、最大の連続深さを求める
    int maxContinuousDepth = 0;
    for (const auto& [c, range] : colRange) {
        int minR = range.first;
        int maxR = range.second;
        bool isContinuous = true;
        
        for (int r = minR; r <= maxR; ++r) {
            if (colCells[c].find(r) == colCells[c].end()) {
                isContinuous = false;
                break;
            }
        }
        
        if (isContinuous) {
            maxContinuousDepth = std::max(maxContinuousDepth, maxR - minR + 1);
        }
    }
    
    eval.depth = static_cast<float>(maxContinuousDepth);

    // 完成度の評価
    if (comp.width == 1 && maxContinuousDepth >= EvalWeights::WELL_MIN_DEPTH) {
        eval.completeness = 1.0f;
    } else if (comp.width == 1 && maxContinuousDepth >= 2) {
        eval.completeness = 0.5f + (maxContinuousDepth - 2) * 0.25f;
    } else {
        eval.completeness = 0.0f;
    }

    // 到達可能性（既にBFSで到達可能なものだけなので1.0）
    eval.accessibility = 1.0f;

    // 非線形なスコア計算
    int cappedDepth = std::min(maxContinuousDepth, EvalWeights::WELL_MAX_DEPTH);
    float depthValue;
    if (cappedDepth >= EvalWeights::WELL_MIN_DEPTH) {
        depthValue = EvalWeights::WELL_BASE_VALUE
                   + static_cast<float>(cappedDepth - EvalWeights::WELL_MIN_DEPTH) * EvalWeights::WELL_DEPTH_MARGIN;
    } else {
        // 4未満の場合は比例配分
        depthValue = static_cast<float>(cappedDepth) * (EvalWeights::WELL_BASE_VALUE / static_cast<float>(EvalWeights::WELL_MIN_DEPTH));
    }
    
    eval.score = eval.completeness * eval.accessibility * depthValue;

    return eval;
}

// ---- 全ての穴を評価 ----
float evaluateAllHoles(const BoardBits& board, const std::deque<PType>& next, bool hasHoldI) {
    auto info = analyzeReachableSpaces(board);

    float totalScore = 0.0f;

    // Iピースが利用可能かどうかを事前に判定
    bool hasI = hasHoldI;
    for (int i = 0; i < std::min(3, static_cast<int>(next.size())); ++i) {
        if (next[i] == PType::I) {
            hasI = true;
            break;
        }
    }

    // テトリスの穴を評価
    for (const auto& well : info.tetrisWells) {
        auto wellEval = evaluateTetrisWell(well, board);
        totalScore += wellEval.score;
    }

    // IピースボーナスはWellが存在する場合に一度だけ加算
    if (hasI && !info.tetrisWells.empty()) {
        totalScore += EvalWeights::WELL_I_BONUS;
    }

    // その他の穴を評価（ペナルティとして減算）
    for (const auto& hole : info.otherHoles) {
        auto holeEval = evaluateHole(hole, board);
        totalScore -= holeEval.totalScore;
    }

    return totalScore;
}

// ---- TSD候補を評価 ----
float evaluateTSDCandidates(const BoardBits& board, const std::deque<PType>& next, bool hasHoldT) {
    float score = 0.0f;

    // Tピースが次に来るか、またはHoldにあるか
    bool hasT = hasHoldT;
    for (int i = 0; i < std::min(3, static_cast<int>(next.size())); ++i) {
        if (next[i] == PType::T) {
            hasT = true;
            break;
        }
    }

    if (!hasT) return 0.0f;  // TピースがなければTSDは不可能

    // TSDのセットアップを探す
    for (int rot = 0; rot < 4; ++rot) {
        const MinoShape& shape = SHAPES[static_cast<int>(PType::T)][rot];

        for (int x = -2; x < BOARD_W + 2; ++x) {
            int y = HardDropY(board, shape, x);
            if (y < 0) continue;

            // T-Spinかどうか
            if (!isTSpin(board, x, y, rot)) continue;

            // ボードをシミュレート
            BoardBits temp = board;
            for (int r = 0; r < shape.height; ++r) {
                int row = y + r;
                if (row < 0 || row >= BOARD_H) continue;
                uint16_t mask = shape.rows[r];
                if (x >= 0) {
                    mask <<= x;
                } else {
                    mask >>= (-x);
                }
                temp[row] |= mask;
            }

            // ライン消去数を確認してスコア加算
            int cleared = ClearLines(temp);
            if (cleared == 2) {
                score += EvalWeights::TSD_SCORE;  // TSD
            } else if (cleared == 1) {
                score += EvalWeights::TSS_SCORE;  // T-Spin Single
            } else if (cleared == 3) {
                score += EvalWeights::TST_SCORE;  // T-Spin Triple
            }
        }
    }

    return score;
}

// ---- 地形（Surface）の評価 ----
float evaluateSurface(const BoardBits& board) {
    float score = 0.0f;

    // 各列の高さを取得
    std::vector<int> heights;
    heights.reserve(BOARD_W);
    for (int c = 0; c < BOARD_W; ++c) {
        heights.push_back(getColumnHeight(board, c));
    }

    // 高さのばらつきを評価
    float sum = std::accumulate(heights.begin(), heights.end(), 0.0f);
    float avgHeight = sum / static_cast<float>(BOARD_W);
    
    float variance = 0.0f;
    for (int h : heights) {
        float diff = static_cast<float>(h) - avgHeight;
        variance += diff * diff;
    }
    variance /= static_cast<float>(BOARD_W);

    // ばらつきが小さいほどボーナス
    score -= variance * EvalWeights::VARIANCE_PENALTY;

    // 中央の列が低いほどボーナス
    int mid = BOARD_W / 2;
    int centerHeight = (heights[mid - 1] + heights[mid]) / 2;
    if (centerHeight < static_cast<int>(avgHeight)) {
        score += (avgHeight - static_cast<float>(centerHeight)) * EvalWeights::CENTER_LOW_BONUS;
    }

    return score;
}

// ---- ライン消去後の維持 ----
float evaluatePostClear(const BoardBits& board) {
    BoardBits temp = board;
    int cleared = ClearLines(temp);

    // ライン消去が発生しない場合は評価しない
    if (cleared == 0) return 0.0f;

    // ライン消去後の地形を評価
    float postClearScore = evaluateSurface(temp);

    // ライン消去が多いほどボーナス
    if (cleared >= 2) {
        postClearScore += static_cast<float>(cleared) * EvalWeights::CLEAR_BONUS;
    }

    return postClearScore;
}

// ---- 全てを統合した地形評価 ----
float evaluateTerrainWithHoles(const BoardBits& board, const std::deque<PType>& next, bool hasHoldI, bool hasHoldT) {
    float score = 0.0f;

    // 穴の評価
    score += evaluateAllHoles(board, next, hasHoldI);

    // TSD候補の評価
    score += evaluateTSDCandidates(board, next, hasHoldT);

    // 地形の評価
    score += evaluateSurface(board);

    // 横パリティの評価
    int hParity = calculateHorizontalParity(board);
    if (hParity % 4 == 1 || hParity % 4 == 3) {
        score -= EvalWeights::PARITY_PENALTY;  // パフェ不可能
    }

    return score;
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
    
    // Add horizontal parity as a feature
    v.push_back(calculateHorizontalParity(board));
    
    // Add spin evaluation as a feature
    SpinEvaluator evaluator(false);
    v.push_back(evaluator.evaluate(board, next));
    
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