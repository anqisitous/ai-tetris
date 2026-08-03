// ===================================================================
// pc_search.h - パーフェクトクリア (パフェ) 探索
// ===================================================================
//
// 普通のパフェ探索アルゴリズム:
//   下から h 段 (h = 1..maxRows) を対象領域として、
//   ネクスト+ホールドのミノを深さ優先で置いていき、盤面が空になる手順を探す。
//   枝刈り:
//     1. 空きマス数が4の倍数でない
//     2. 必要ミノ数が残りのミノ数を超える
//     3. 対象領域より上にブロックが残る
//     4. 空きマスの連結成分の大きさが4の倍数でない
//     5. 横パリティの必要条件を満たさない (pc_parity.h)
//   同一局面 (盤面 + 消費数 + ホールド) はメモ化してスキップする。
//
#pragma once
#include "game_engine.h"
#include <cstddef>
#include <deque>
#include <vector>

// ---- パフェ手順の1手 ----
struct PCMove {
    PType type = PType::I;  // 実際に置いたミノ
    int x = 0;
    int y = 0;
    int rot = 0;
    bool usedHold = false;  // ホールドから出したか
    int linesCleared = 0;
};

// ---- 探索オプション ----
struct PCSearchOptions {
    int maxRows = 4;              // パフェを狙う最大段数 (下から)
    bool allowHold = true;        // ホールドを使うか
    bool useParityPruning = true; // 横パリティによる枝刈り
    size_t nodeLimit = 50000;     // 展開ノード数の上限（実戦でフレームを止めない程度）
};

// ---- 探索結果 ----
struct PCSearchResult {
    bool found = false;
    int rowsUsed = 0;             // 対象にした段数
    std::vector<PCMove> moves;    // 見つかった手順
    size_t nodesVisited = 0;
    bool limitReached = false;    // ノード上限で打ち切ったか
};

// パフェ手順を探す。見つからなければ found = false。
PCSearchResult findPerfectClear(const BoardBits& board, const std::deque<PType>& queue,
                                PType hold, bool canHold,
                                const PCSearchOptions& options = PCSearchOptions());

// 探索前の軽量チェック (マス数と横パリティの必要条件のみ)
bool hasPerfectClearChance(const BoardBits& board, const std::deque<PType>& queue,
                           PType hold, bool canHold, int maxRows = 4);

// 盤面の一番高い段 (ブロックのある最上段の段番号)。空なら BOARD_H。
int getStackTopRow(const BoardBits& board);

// 空きマスの連結成分がすべて4の倍数か (領域 [topRow, BOARD_H-1] を対象)
bool emptyRegionsDivisibleByFour(const BoardBits& board, int topRow);
