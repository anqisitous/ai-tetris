// ===================================================================
// test_fumen_decoder.cpp - Unit tests for fumen decoder/encoder round-trip
// ===================================================================
//
// このテストは主に FIELD_H の回帰を検知するために存在する。
// fumen v1.15仕様のフィールド高さは24行(可視20行+バッファ4行)であり、
// 23と取り違えると decodeFieldDiff の走査ブロック数がずれ、以降の
// action/comment解析が全て破綻して例外が飛ぶか、誤った盤面を返す。
// 単純なデコード成功だけでなく、盤面を再エンコードして元の文字列と
// 完全一致することまで確認することで、この種のオフバイワン回帰を
// 確実に検出する。
// ===================================================================
#include "../../fumen_decoder.h"
#include "../fixtures/test_fixtures.cpp"
#include <array>
#include <iostream>
#include <string>

namespace FumenDecoderTests {

namespace {

const std::string ENCODE_TABLE =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr int TABLE_LEN = 64;

// ---- 最小エンコーダ: 1ページ・コメントなし・盤面のみのfumenだけを組み立てる ----
// decode()との往復一致検証専用。ミノ配置や複数ページには対応しない。
class OutBuffer {
public:
    void push(long long value, int splitCount) {
        long long current = value;
        for (int i = 0; i < splitCount; ++i) {
            values.push_back(static_cast<int>(current % TABLE_LEN));
            current /= TABLE_LEN;
        }
    }
    std::string toString() const {
        std::string out;
        out.reserve(values.size());
        for (int v : values) out += ENCODE_TABLE[v];
        return out;
    }
private:
    std::vector<int> values;
};

void encodeFieldDiff(const std::array<int, FumenDecoder::FIELD_BLOCKS>& prev,
                      const std::array<int, FumenDecoder::FIELD_BLOCKS>& cur,
                      OutBuffer& out) {
    using namespace FumenDecoder;
    auto getAt = [&](const std::array<int, FIELD_BLOCKS>& f, int idx) {
        int x = idx % FIELD_W;
        int y = FIELD_H - 1 - (idx / FIELD_W);
        return f[x + y * FIELD_W];
    };

    long long prevDiff = static_cast<long long>(getAt(cur, 0)) - getAt(prev, 0) + 8;
    long long counter = -1;

    for (int idx = 0; idx < FIELD_BLOCKS; ++idx) {
        long long diff = static_cast<long long>(getAt(cur, idx)) - getAt(prev, idx) + 8;
        if (diff != prevDiff) {
            out.push(prevDiff * FIELD_BLOCKS + counter, 2);
            counter = 0;
            prevDiff = diff;
        } else {
            counter++;
        }
    }
    out.push(prevDiff * FIELD_BLOCKS + counter, 2);
}

long long encodeAction(int type, int rotation, long long posVal,
                        bool rise, bool mirror, bool colorize,
                        bool comment, bool lock) {
    long long v = lock ? 0 : 1;
    v = v * 2 + (comment ? 1 : 0);
    v = v * 2 + (colorize ? 1 : 0);
    v = v * 2 + (mirror ? 1 : 0);
    v = v * 2 + (rise ? 1 : 0);
    v = v * FumenDecoder::FIELD_BLOCKS + posVal;
    v = v * 4 + rotation;
    v = v * 8 + type;
    return v;
}

// 盤面のみ(ミノなし)の1ページfumenを組み立てる。
// Emptyミノのposition規約: nx=0, ny=FIELD_H-1 -> posVal=0。
// colorizeは「先頭ページかどうか」で決まり、1ページ完結のfumenでは常にtrue。
std::string encodeSingleFieldOnlyPage(const std::array<int, FumenDecoder::FIELD_BLOCKS>& field) {
    OutBuffer out;
    std::array<int, FumenDecoder::FIELD_BLOCKS> emptyPrev{};
    encodeFieldDiff(emptyPrev, field, out);

    long long posVal = 0;  // nx=0, ny=FIELD_H-1
    long long actionVal = encodeAction(/*type=*/0, /*rotation=*/0, posVal,
                                        false, false, /*colorize=*/true,
                                        false, /*lock=*/true);
    out.push(actionVal, 3);
    return out.toString();
}

} // namespace

// ===================================================================
// FIELD_H 回帰テスト: 実際のfumen URLをデコードできるか
// ===================================================================

void testDecode_RealFumenURL_DoesNotThrow() {
    // https://fumen.zui.jp/?v115@... で実際に共有されたfumen。
    // FIELD_Hが23のままだとdecodeFieldDiffのブロック数がずれ、
    // 以降のバッファ読み取りが破綻して空のvectorが返る(例外は握りつぶされる)。
    std::string fumen = "v115@Mhg0Deg0DeQpBeRpg0RpBeRpAeRpJeAgH";
    auto pages = FumenDecoder::decode(fumen);
    TestFixtures::assertTrue(!pages.empty(),
        "Decoding a valid single-page fumen must not silently fail (FIELD_H must be 24)");
    TestFixtures::assertEqual(1, static_cast<int>(pages.size()),
        "This fumen encodes exactly one page");
}

void testDecode_RealFumenURL_BoardShape() {
    std::string fumen = "v115@Mhg0Deg0DeQpBeRpg0RpBeRpAeRpJeAgH";
    auto pages = FumenDecoder::decode(fumen);
    TestFixtures::assertTrue(!pages.empty(), "Decode must succeed before checking board shape");
    if (pages.empty()) return;

    const auto& page = pages[0];
    // y=1..3 (下から2〜4段目) にブロックがあり、y=0とy>=4は空であることを確認する。
    // (FIELD_H取り違えバグでは行のインデックス自体がずれるため、
    //  この形状チェックだけでも1行ズレを検出できる)
    auto rowHasBlock = [&](int y) {
        for (int x = 0; x < FumenDecoder::FIELD_W; ++x) {
            if (page.fieldRaw[x + y * FumenDecoder::FIELD_W] != 0) return true;
        }
        return false;
    };

    TestFixtures::assertFalse(rowHasBlock(0), "Bottom row (y=0) should be empty for this fumen");
    TestFixtures::assertTrue(rowHasBlock(1), "Row y=1 should contain blocks");
    TestFixtures::assertTrue(rowHasBlock(2), "Row y=2 should contain blocks");
    TestFixtures::assertTrue(rowHasBlock(3), "Row y=3 should contain blocks");
    TestFixtures::assertFalse(rowHasBlock(4), "Row y=4 should be empty for this fumen");
}

// ===================================================================
// 往復一致テスト: decode -> encode -> 元の文字列と完全一致
// ===================================================================

void testRoundTrip_ExactStringMatch() {
    std::string original = "v115@Mhg0Deg0DeQpBeRpg0RpBeRpAeRpJeAgH";

    auto pages = FumenDecoder::decode(original);
    TestFixtures::assertTrue(!pages.empty(), "Original fumen must decode successfully");
    if (pages.empty()) return;

    std::array<int, FumenDecoder::FIELD_BLOCKS> field{};
    for (int i = 0; i < FumenDecoder::FIELD_BLOCKS; ++i) field[i] = pages[0].fieldRaw[i];

    std::string reEncoded = "v115@" + encodeSingleFieldOnlyPage(field);

    // 文字列として完全一致することは、FIELD_H・posValのny規約・
    // colorizeフラグの扱いなど、デコーダ側の複数の定数が仕様通りである
    // ことの最も強い保証になる。
    TestFixtures::assertTrue(reEncoded == original,
        "Re-encoding the decoded board must reproduce the exact original fumen string. "
        "original=" + original + " reencoded=" + reEncoded);
}

void testRoundTrip_ReDecodedBoardMatches() {
    std::string original = "v115@Mhg0Deg0DeQpBeRpg0RpBeRpAeRpJeAgH";

    auto pages1 = FumenDecoder::decode(original);
    TestFixtures::assertTrue(!pages1.empty(), "Original fumen must decode successfully");
    if (pages1.empty()) return;

    std::array<int, FumenDecoder::FIELD_BLOCKS> field{};
    for (int i = 0; i < FumenDecoder::FIELD_BLOCKS; ++i) field[i] = pages1[0].fieldRaw[i];

    std::string reEncoded = "v115@" + encodeSingleFieldOnlyPage(field);
    auto pages2 = FumenDecoder::decode(reEncoded);

    TestFixtures::assertTrue(!pages2.empty(), "Re-encoded fumen must also decode successfully");
    if (pages2.empty()) return;

    bool occupancyMatches = true;
    for (int i = 0; i < FumenDecoder::FIELD_BLOCKS; ++i) {
        bool a = pages1[0].fieldRaw[i] != 0;
        bool b = pages2[0].fieldRaw[i] != 0;
        if (a != b) { occupancyMatches = false; break; }
    }
    TestFixtures::assertTrue(occupancyMatches,
        "Board occupancy must survive a decode -> encode -> decode round trip");
}

// ===================================================================
// pageToBoardBits / pageToTopNRows の基本チェック
// ===================================================================

void testPageToBoardBits_PreservesOccupiedCells() {
    std::string fumen = "v115@Mhg0Deg0DeQpBeRpg0RpBeRpAeRpJeAgH";
    auto pages = FumenDecoder::decode(fumen);
    TestFixtures::assertTrue(!pages.empty(), "Decode must succeed before board conversion");
    if (pages.empty()) return;

    BoardBits board = FumenDecoder::pageToBoardBits(pages[0]);
    // fumen側のy=1..3にあるブロックが、BoardBits側の同じ行インデックスに
    // (下から積む規約で)そのまま反映されていることを確認する。
    TestFixtures::assertTrue(board[0] == 0, "BoardBits row 0 should be empty");
    TestFixtures::assertTrue(board[1] != 0, "BoardBits row 1 should contain blocks");
    TestFixtures::assertTrue(board[2] != 0, "BoardBits row 2 should contain blocks");
    TestFixtures::assertTrue(board[3] != 0, "BoardBits row 3 should contain blocks");
}

// ===================================================================
// Run all fumen decoder tests
// ===================================================================

void runAllTests() {
    std::cout << "Running Fumen Decoder Tests..." << std::endl;

    testDecode_RealFumenURL_DoesNotThrow();
    testDecode_RealFumenURL_BoardShape();

    testRoundTrip_ExactStringMatch();
    testRoundTrip_ReDecodedBoardMatches();

    testPageToBoardBits_PreservesOccupiedCells();

    std::cout << "All Fumen Decoder tests passed!" << std::endl;
}

} // namespace FumenDecoderTests
