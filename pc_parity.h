// ===================================================================
// pc_parity.h - 横パリティ (Horizontal Parity) の分類とパフェ必要条件
// ===================================================================
//
// 横パリティの定義
//   盤面の「段(row)」を、その段に埋まっているマス数が奇数か偶数かで分類する。
//   盤面は10列なので、埋め切った段のマス数は必ず 10 (偶数) になる。
//   ミノは1個あたり4マス(偶数)なので、盤面全体のマス数の偶奇は不変であり、
//   「奇数パリティ段の個数」は常に偶数でなければならない。
//   (= 奇数パリティの合計が奇数個になる盤面はパフェ不能)
//
// ミノ(種類×向き)の分類 -- SHAPES から自動導出する
//   奇数パリティ: I(90/270)            奇数マスを足す段が 4 段
//   偶数パリティ: I(0/180), O, S/Z(0/180)  奇数マスを足す段が 0 段
//   2:2         : T/J/L(全向き), S/Z(90/270) 奇数マスを足す段が 2 段
//
#pragma once
#include "game_engine.h"
#include <string>
#include <vector>

// ---- ミノのパリティ種別 ----
enum class MinoParity {
    Odd,   // 奇数パリティ (奇数マスを足す段が4)
    Even,  // 偶数パリティ (奇数マスを足す段が0)
    Mixed  // 2:2         (奇数マスを足す段が2)
};

// (種類, 向き) が奇数マスを足す段の数。必ず 0 / 2 / 4 のいずれか。
int countOddRowContributions(PType type, int rot);

// (種類, 向き) のパリティ種別
MinoParity getMinoParity(PType type, int rot);

// 表示用の名前
const char* minoParityName(MinoParity parity);

// その種類のミノが、いずれかの向きで指定のパリティになれるか
bool canTakeParity(PType type, MinoParity parity);

// ---- 盤面の横パリティ情報 ----
struct HorizontalParityInfo {
    int topRow = 0;                    // 解析対象の最上段
    int bottomRow = 0;                 // 解析対象の最下段
    int oddRows = 0;                   // 奇数パリティ段の数
    int evenRows = 0;                  // 偶数パリティ段の数(空段も含む)
    std::vector<int> oddRowIndices;    // 奇数パリティ段の段番号
    int emptyCells = 0;                // 対象領域の空きマス数
    int minoBudget = 0;                // 埋めるのに必要なミノ数 (emptyCells / 4)
    bool cellCountOk = false;          // 空きマス数が4の倍数か
    bool oddRowCountOk = false;        // 奇数パリティ段が偶数個か

    bool valid() const { return cellCountOk && oddRowCountOk; }
};

// 段 [topRow, bottomRow] を対象に横パリティを解析する
HorizontalParityInfo analyzeHorizontalParity(const BoardBits& board, int topRow, int bottomRow);

// 一番上のブロックがある段から最下段までを対象にする版
HorizontalParityInfo analyzeHorizontalParity(const BoardBits& board);

// 奇数パリティ段の数だけを返す軽量版
int countOddParityRows(const BoardBits& board);

// ---- 使用ミノのパリティ内訳 ----
struct ParityCombination {
    int oddMinos = 0;    // 奇数パリティのミノ数   I(90/270)
    int evenMinos = 0;   // 偶数パリティのミノ数   I(0/180), O, S/Z(0/180)
    int mixedMinos = 0;  // 2:2 のミノ数           T/J/L, S/Z(90/270)

    int totalMinos() const { return oddMinos + evenMinos + mixedMinos; }

    // 奇数パリティ段を解消できる上限 (奇数ミノは4段、2:2は2段に効く)
    int oddRowCoverage() const { return 4 * oddMinos + 2 * mixedMinos; }

    // 「奇数パリティのミノが奇数個なら偶数パリティのミノも奇数個」という対応規則
    bool followsOddCountRule() const { return (oddMinos % 2) == (evenMinos % 2); }

    std::string describe() const;
};

// 盤面ケースに対してパリティ条件を満たす内訳を列挙する。
// 既定で最大12通り (規則を満たすものを優先し、奇数ミノの少ない順)。
std::vector<ParityCombination> enumerateParityCombinations(const HorizontalParityInfo& info,
                                                           int maxResults = 12);

// 手持ちのミノ集合で、その内訳を実際に組めるか
// (奇数はIのみ、2:2はT/J/L/S/Z、偶数はI/O/S/Z が担当できる)
bool isCombinationReachable(const std::vector<PType>& pieces, const ParityCombination& combo);

// パフェ可能性のパリティ必要条件
bool isPerfectClearParityPossible(const HorizontalParityInfo& info,
                                  const std::vector<PType>& pieces);
