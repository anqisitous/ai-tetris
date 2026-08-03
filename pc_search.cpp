// ===================================================================
// pc_search.cpp - パーフェクトクリア (パフェ) 探索の実装
// ===================================================================
#include "pc_search.h"
#include "pc_parity.h"
#include <algorithm>
#include <unordered_set>

namespace {

int countFilled(const BoardBits& board, int topRow, int bottomRow) {
    int filled = 0;
    for (int r = topRow; r <= bottomRow; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            if (board[r] & (1 << c)) ++filled;
        }
    }
    return filled;
}

bool isBoardEmpty(const BoardBits& board) {
    for (int r = 0; r < BOARD_H; ++r) {
        if (board[r] != 0) return false;
    }
    return true;
}

bool hasBlocksAbove(const BoardBits& board, int topRow) {
    for (int r = 0; r < topRow; ++r) {
        if (board[r] != 0) return true;
    }
    return false;
}

uint64_t hashState(const BoardBits& board, int topRow, size_t index, PType hold, bool canHold) {
    uint64_t h = 1469598103934665603ULL;
    for (int r = topRow; r < BOARD_H; ++r) {
        h = (h ^ static_cast<uint64_t>(board[r])) * 1099511628211ULL;
    }
    h = (h ^ (index * 131ULL)) * 1099511628211ULL;
    h = (h ^ static_cast<uint64_t>(hold)) * 1099511628211ULL;
    h = (h ^ (canHold ? 0x9E3779B9ULL : 0x85EBCA6BULL)) * 1099511628211ULL;
    return h;
}

// 残りのミノ (ネクスト + ホールド) を列挙する
std::vector<PType> remainingPieces(const std::deque<PType>& queue, size_t index,
                                   PType hold, bool canHold) {
    std::vector<PType> pieces;
    for (size_t i = index; i < queue.size(); ++i) pieces.push_back(queue[i]);
    if (canHold) pieces.push_back(hold);
    return pieces;
}

// 下から h 段を対象にしたとき、まだパフェの必要条件を満たしているか
bool windowFeasible(const BoardBits& board, int h, const std::vector<PType>& pieces,
                    bool useParityPruning) {
    const int topRow = BOARD_H - h;
    const int empty = BOARD_W * h - countFilled(board, topRow, BOARD_H - 1);
    if (empty % 4 != 0) return false;
    if (empty / 4 > static_cast<int>(pieces.size())) return false;
    if (!emptyRegionsDivisibleByFour(board, topRow)) return false;

    if (useParityPruning) {
        HorizontalParityInfo info = analyzeHorizontalParity(board, topRow, BOARD_H - 1);
        if (!isPerfectClearParityPossible(info, pieces)) return false;
    }
    return true;
}

// ラインが消えると残り段数が変わるため、対象段数は毎回 [積み高さ, maxRows] を試す
bool anyWindowFeasible(const BoardBits& board, int maxRows, const std::vector<PType>& pieces,
                       bool useParityPruning) {
    const int stackTop = getStackTopRow(board);
    if (stackTop == BOARD_H) return true;  // 既に空

    const int stackHeight = BOARD_H - stackTop;
    if (stackHeight > maxRows) return false;

    for (int h = stackHeight; h <= maxRows; ++h) {
        if (windowFeasible(board, h, pieces, useParityPruning)) return true;
    }
    return false;
}

struct SearchContext {
    const std::deque<PType>* queue = nullptr;
    PCSearchOptions options;
    int topRow = 0;  // これより上にブロックが残ってはいけない段
    size_t nodes = 0;
    bool limitReached = false;
    std::unordered_set<uint64_t> visited;
    std::vector<PCMove> moves;
};

bool dfs(SearchContext& ctx, const BoardBits& board, size_t index, PType hold, bool canHold);

// 1手分の候補を試す
bool tryPlacements(SearchContext& ctx, const BoardBits& board, size_t index,
                   PType hold, PType pieceType, bool usedHold) {
    std::vector<PlacementResult> placements =
        EnumerateAllPlacements(board, pieceType, false, pieceType, 0, 0);

    // ラインが消える手と低い位置の手を先に試すと解が早く見つかりやすい
    std::stable_sort(placements.begin(), placements.end(),
                     [](const PlacementResult& a, const PlacementResult& b) {
                         if (a.linesCleared != b.linesCleared) return a.linesCleared > b.linesCleared;
                         return a.y > b.y;
                     });

    const PType nextHold = usedHold ? (*ctx.queue)[index] : hold;

    for (const PlacementResult& p : placements) {
        if (hasBlocksAbove(p.board, ctx.topRow)) continue;

        PCMove move;
        move.type = pieceType;
        move.x = p.x;
        move.y = p.y;
        move.rot = p.rot;
        move.usedHold = usedHold;
        move.linesCleared = p.linesCleared;

        ctx.moves.push_back(move);
        if (dfs(ctx, p.board, index + 1, nextHold, ctx.options.allowHold)) return true;
        ctx.moves.pop_back();

        if (ctx.limitReached) return false;
    }
    return false;
}

bool dfs(SearchContext& ctx, const BoardBits& board, size_t index, PType hold, bool canHold) {
    if (isBoardEmpty(board)) return true;

    if (ctx.nodes >= ctx.options.nodeLimit) {
        ctx.limitReached = true;
        return false;
    }
    ++ctx.nodes;

    if (index >= ctx.queue->size()) return false;
    if (hasBlocksAbove(board, ctx.topRow)) return false;

    std::vector<PType> pieces = remainingPieces(*ctx.queue, index, hold, canHold);
    if (!anyWindowFeasible(board, ctx.options.maxRows, pieces, ctx.options.useParityPruning)) {
        return false;
    }

    const uint64_t key = hashState(board, ctx.topRow, index, hold, canHold);
    if (!ctx.visited.insert(key).second) return false;

    const PType current = (*ctx.queue)[index];
    if (tryPlacements(ctx, board, index, hold, current, false)) return true;
    if (ctx.limitReached) return false;

    if (canHold && ctx.options.allowHold && hold != current) {
        if (tryPlacements(ctx, board, index, hold, hold, true)) return true;
    }
    return false;
}

}  // namespace

// ---- 一番高い段 ----
int getStackTopRow(const BoardBits& board) {
    for (int r = 0; r < BOARD_H; ++r) {
        if (board[r] != 0) return r;
    }
    return BOARD_H;
}

// ---- 空きマスの連結成分の大きさが全て4の倍数か ----
bool emptyRegionsDivisibleByFour(const BoardBits& board, int topRow) {
    const int rows = BOARD_H - topRow;
    if (rows <= 0) return true;

    std::vector<char> seen(static_cast<size_t>(rows) * BOARD_W, 0);
    auto filled = [&](int r, int c) { return (board[r] & (1 << c)) != 0; };

    for (int r = topRow; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            size_t idx = static_cast<size_t>(r - topRow) * BOARD_W + c;
            if (seen[idx] || filled(r, c)) continue;

            int size = 0;
            std::vector<std::pair<int, int>> stack{{r, c}};
            seen[idx] = 1;
            while (!stack.empty()) {
                auto [cr, cc] = stack.back();
                stack.pop_back();
                ++size;
                const int dr[4] = {-1, 1, 0, 0};
                const int dc[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; ++d) {
                    int nr = cr + dr[d];
                    int nc = cc + dc[d];
                    if (nr < topRow || nr >= BOARD_H || nc < 0 || nc >= BOARD_W) continue;
                    size_t nidx = static_cast<size_t>(nr - topRow) * BOARD_W + nc;
                    if (seen[nidx] || filled(nr, nc)) continue;
                    seen[nidx] = 1;
                    stack.push_back({nr, nc});
                }
            }
            if (size % 4 != 0) return false;
        }
    }
    return true;
}

// ---- 軽量チェック ----
bool hasPerfectClearChance(const BoardBits& board, const std::deque<PType>& queue,
                           PType hold, bool canHold, int maxRows) {
    if (getStackTopRow(board) == BOARD_H) return true;  // 既に空
    std::vector<PType> pieces = remainingPieces(queue, 0, hold, canHold);
    return anyWindowFeasible(board, maxRows, pieces, true);
}

// ---- パフェ探索 ----
PCSearchResult findPerfectClear(const BoardBits& board, const std::deque<PType>& queue,
                                PType hold, bool canHold,
                                const PCSearchOptions& options) {
    PCSearchResult result;

    if (getStackTopRow(board) == BOARD_H) {
        result.found = true;  // 既にパフェ状態
        return result;
    }
    if (queue.empty()) return result;

    const bool useHold = canHold && options.allowHold;
    if (!anyWindowFeasible(board, options.maxRows, remainingPieces(queue, 0, hold, useHold),
                           options.useParityPruning)) {
        return result;
    }

    SearchContext ctx;
    ctx.queue = &queue;
    ctx.options = options;
    ctx.topRow = std::max(0, BOARD_H - options.maxRows);

    const bool found = dfs(ctx, board, 0, hold, useHold);
    result.nodesVisited = ctx.nodes;
    result.limitReached = ctx.limitReached;
    if (found) {
        result.found = true;
        result.moves = ctx.moves;
        for (const PCMove& move : result.moves) result.rowsUsed += move.linesCleared;
    }
    return result;
}
