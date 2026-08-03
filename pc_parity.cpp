// ===================================================================
// pc_parity.cpp - 横パリティ (Horizontal Parity) の実装
// ===================================================================
#include "pc_parity.h"
#include <algorithm>
#include <array>

namespace {

int popcount10(uint16_t bits) {
    int n = 0;
    for (int c = 0; c < BOARD_W; ++c) {
        if (bits & (1 << c)) ++n;
    }
    return n;
}

// パリティを担当できる種類かどうか
bool typeAllowsParity(PType type, MinoParity parity) {
    for (int rot = 0; rot < 4; ++rot) {
        if (getMinoParity(type, rot) == parity) return true;
    }
    return false;
}

// 種類ごとの個数を数える
std::array<int, NUM_PTYPES> countByType(const std::vector<PType>& pieces) {
    std::array<int, NUM_PTYPES> counts{};
    counts.fill(0);
    for (PType t : pieces) {
        int idx = static_cast<int>(t);
        if (idx >= 0 && idx < NUM_PTYPES) ++counts[idx];
    }
    return counts;
}

// 種類を順番に見て、要求された内訳へ割り当てられるかを探索する
bool assign(const std::array<int, NUM_PTYPES>& counts, int typeIdx, int odd, int even, int mixed) {
    if (odd == 0 && even == 0 && mixed == 0) return true;
    if (typeIdx >= NUM_PTYPES) return false;

    PType type = static_cast<PType>(typeIdx);
    int available = counts[typeIdx];
    bool canOdd = typeAllowsParity(type, MinoParity::Odd);
    bool canEven = typeAllowsParity(type, MinoParity::Even);
    bool canMixed = typeAllowsParity(type, MinoParity::Mixed);

    int maxOdd = canOdd ? std::min(odd, available) : 0;
    for (int useOdd = 0; useOdd <= maxOdd; ++useOdd) {
        int maxEven = canEven ? std::min(even, available - useOdd) : 0;
        for (int useEven = 0; useEven <= maxEven; ++useEven) {
            int maxMixed = canMixed ? std::min(mixed, available - useOdd - useEven) : 0;
            for (int useMixed = 0; useMixed <= maxMixed; ++useMixed) {
                if (assign(counts, typeIdx + 1, odd - useOdd, even - useEven, mixed - useMixed)) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

// ---- ミノ(種類×向き)が奇数マスを足す段の数 ----
int countOddRowContributions(PType type, int rot) {
    const MinoShape& shape = SHAPES[static_cast<int>(type)][rot & 3];
    int oddRows = 0;
    for (int r = 0; r < shape.height; ++r) {
        if (popcount10(shape.rows[r]) % 2 == 1) ++oddRows;
    }
    return oddRows;
}

// ---- ミノのパリティ種別 ----
MinoParity getMinoParity(PType type, int rot) {
    switch (countOddRowContributions(type, rot)) {
        case 0:  return MinoParity::Even;
        case 4:  return MinoParity::Odd;
        default: return MinoParity::Mixed;  // 2
    }
}

const char* minoParityName(MinoParity parity) {
    switch (parity) {
        case MinoParity::Odd:  return "odd";
        case MinoParity::Even: return "even";
        default:               return "mixed";
    }
}

bool canTakeParity(PType type, MinoParity parity) {
    return typeAllowsParity(type, parity);
}

// ---- 盤面の横パリティ解析 ----
HorizontalParityInfo analyzeHorizontalParity(const BoardBits& board, int topRow, int bottomRow) {
    HorizontalParityInfo info;
    info.topRow = std::max(0, topRow);
    info.bottomRow = std::min(BOARD_H - 1, bottomRow);

    for (int r = info.topRow; r <= info.bottomRow; ++r) {
        int filled = popcount10(board[r]);
        info.emptyCells += BOARD_W - filled;
        if (filled % 2 == 1) {
            ++info.oddRows;
            info.oddRowIndices.push_back(r);
        } else {
            ++info.evenRows;
        }
    }

    info.cellCountOk = (info.emptyCells % 4 == 0);
    info.oddRowCountOk = (info.oddRows % 2 == 0);
    info.minoBudget = info.emptyCells / 4;
    return info;
}

HorizontalParityInfo analyzeHorizontalParity(const BoardBits& board) {
    int topRow = BOARD_H - 1;
    for (int r = 0; r < BOARD_H; ++r) {
        if (board[r] != 0) { topRow = r; break; }
    }
    return analyzeHorizontalParity(board, topRow, BOARD_H - 1);
}

int countOddParityRows(const BoardBits& board) {
    int oddRows = 0;
    for (int r = 0; r < BOARD_H; ++r) {
        if (popcount10(board[r]) % 2 == 1) ++oddRows;
    }
    return oddRows;
}

// ---- 内訳の説明文 ----
std::string ParityCombination::describe() const {
    std::string s = "odd=" + std::to_string(oddMinos) +
                    " even=" + std::to_string(evenMinos) +
                    " mixed=" + std::to_string(mixedMinos) +
                    " total=" + std::to_string(totalMinos()) +
                    " coverage=" + std::to_string(oddRowCoverage());
    s += followsOddCountRule() ? " rule=ok" : " rule=ng";
    return s;
}

// ---- パリティ内訳の列挙 ----
std::vector<ParityCombination> enumerateParityCombinations(const HorizontalParityInfo& info,
                                                           int maxResults) {
    std::vector<ParityCombination> all;
    if (!info.valid() || maxResults <= 0) return all;

    const int total = info.minoBudget;
    const int oddRows = info.oddRows;

    for (int odd = 0; odd <= total; ++odd) {
        for (int mixed = 0; mixed + odd <= total; ++mixed) {
            ParityCombination combo;
            combo.oddMinos = odd;
            combo.mixedMinos = mixed;
            combo.evenMinos = total - odd - mixed;
            // 奇数パリティ段を解消しきれない内訳は不可
            if (combo.oddRowCoverage() < oddRows) continue;
            all.push_back(combo);
        }
    }

    // 規則を満たすものを優先し、奇数ミノ・2:2ミノの少ない順に並べる
    std::stable_sort(all.begin(), all.end(),
                     [](const ParityCombination& a, const ParityCombination& b) {
                         if (a.followsOddCountRule() != b.followsOddCountRule()) {
                             return a.followsOddCountRule();
                         }
                         if (a.oddMinos != b.oddMinos) return a.oddMinos < b.oddMinos;
                         return a.mixedMinos < b.mixedMinos;
                     });

    if (static_cast<int>(all.size()) > maxResults) all.resize(maxResults);
    return all;
}

// ---- 手持ちミノでその内訳を組めるか ----
bool isCombinationReachable(const std::vector<PType>& pieces, const ParityCombination& combo) {
    if (combo.totalMinos() > static_cast<int>(pieces.size())) return false;
    auto counts = countByType(pieces);
    return assign(counts, 0, combo.oddMinos, combo.evenMinos, combo.mixedMinos);
}

// ---- パフェ可能性のパリティ必要条件 ----
bool isPerfectClearParityPossible(const HorizontalParityInfo& info,
                                  const std::vector<PType>& pieces) {
    if (!info.valid()) return false;
    if (info.minoBudget > static_cast<int>(pieces.size())) return false;

    // 全ての内訳を対象に、手持ちで組めるものが1つでもあれば必要条件を満たす
    const int noLimit = (info.minoBudget + 1) * (info.minoBudget + 2);
    for (const ParityCombination& combo : enumerateParityCombinations(info, noLimit)) {
        if (isCombinationReachable(pieces, combo)) return true;
    }
    return false;
}
