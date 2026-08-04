// ===================================================================
// fumen_decoder.h - fumen(テト譜) デコーダー 公開インターフェース
//
// fumen_decoder.cpp が要求する型・定数をここに集約する。
// BoardBits / BOARD_BUFFER は game_engine.h の定義をそのまま再利用し、
// 二重定義を避ける。
// ===================================================================
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

#include "game_engine.h"  // BoardBits, BOARD_BUFFER

namespace FumenDecoder {

// fumen(v115)のフィールド仕様: 幅10 x 高さ23 (ガベージ行含む可視領域)
constexpr int FIELD_W = 10;
constexpr int FIELD_H = 23;
constexpr int FIELD_BLOCKS = FIELD_W * FIELD_H;

// decode() が返す1ページ分の地形スナップショット
// fieldRaw[x + y * FIELD_W] : y=0が最下段、y=FIELD_H-1が最上段
struct DecodedPage {
    std::array<uint8_t, FIELD_BLOCKS> fieldRaw{};
};

// fumen文字列(例: "v115@...")を全ページ分デコードする。
// 不正な形式やサポート外バージョンの場合は空のvectorを返す。
std::vector<DecodedPage> decode(const std::string& fumenInput);

// 指定ページの最上段からdepth行(1〜3)を抽出し、10*depthビットに詰めて返す。
// bit index = r * FIELD_W + x (r=0が最上段からの1行目)
std::bitset<30> pageToTopNRows(const DecodedPage& page, int depth);

// 指定ページをゲーム側のBoardBits形式(下から積んだ40行バッファ)に変換する。
BoardBits pageToBoardBits(const DecodedPage& page);

}  // namespace FumenDecoder
