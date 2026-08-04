// ===================================================================
// fumen_decoder.cpp - fumen(テト譜) デコーダー 実装
//
// fumenParse.ts の Buffer / InnerField / decodeFieldDiff / decodeAction /
// decode() をC++に移植したもの。コメント・Quiz・エンコードは省略。
// ===================================================================
#include "fumen_decoder.h"
#include <regex>
#include <stdexcept>

namespace FumenDecoder {

namespace {

// ---- Base64風テーブル (fumenParse.ts の ENCODE_TABLE と同一) ----
const std::string ENCODE_TABLE =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr int TABLE_LEN = 64;

int tableIndexOf(char c) {
    auto pos = ENCODE_TABLE.find(c);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

// ---- Buffer: fumen文字列を数値列として読み出す ----
class Buffer {
public:
    explicit Buffer(const std::string& data) {
        values.reserve(data.size());
        for (char c : data) {
            int idx = tableIndexOf(c);
            if (idx < 0) throw std::runtime_error("invalid fumen character");
            values.push_back(idx);
        }
        pos = 0;
    }

    long long poll(int maxCount) {
        long long value = 0;
        long long mul = 1;
        for (int i = 0; i < maxCount; ++i) {
            if (pos >= values.size()) throw std::runtime_error("unexpected end of fumen");
            value += static_cast<long long>(values[pos]) * mul;
            mul *= TABLE_LEN;
            pos++;
        }
        return value;
    }

    long long pollOne() { return poll(1); }

    bool isEmpty() const { return pos >= values.size(); }

private:
    std::vector<int> values;
    size_t pos;
};

// ---- InnerField: fumenの内部表現 (10x23、ガベージ領域含む) ----
// fumenParse.ts の InnerField/PlayField を簡略化した版。
// ここでは可視フィールド(y>=0)のみを扱う。ガベージ(y<0)はデコード時に
// フィールド差分を正しく読み進めるために必要だが、tmpl用途では
// 最終的に可視フィールドしか使わないため、別配列として保持するに留める。
struct InnerField {
    std::array<int, FIELD_BLOCKS> field{};       // y=0..22 (下から上)
    std::array<int, FIELD_W> garbage{};           // y=-1相当のガベージ1行分

    int getAt(int x, int y) const {
        if (y >= 0) return field[x + y * FIELD_W];
        return garbage[x]; // ガベージは1行分のみ扱う (fumenの一般的な用途で十分)
    }
    void addAt(int x, int y, int diff) {
        if (y >= 0) field[x + y * FIELD_W] += diff;
        else garbage[x] += diff;
    }
};

// ---- フィールド差分デコード (fumenParse.ts の decodeFieldDiff 相当) ----
struct FieldDiffResult {
    InnerField field;
    bool changed;
};

FieldDiffResult decodeFieldDiff(const InnerField& prev, Buffer& buffer) {
    InnerField field = prev;
    int idx = 0;
    bool changed = true;

    while (idx < FIELD_BLOCKS) {
        long long diffBlock = buffer.poll(2);
        long long diff = diffBlock / FIELD_BLOCKS;
        long long count = diffBlock % FIELD_BLOCKS;

        if (diff == 8 && count == FIELD_BLOCKS - 1) changed = false;

        for (long long i = 0; i <= count && idx < FIELD_BLOCKS; ++i) {
            int x = idx % FIELD_W;
            int y = FIELD_H - 1 - (idx / FIELD_W);
            field.addAt(x, y, static_cast<int>(diff - 8));
            idx++;
        }
    }
    return { field, changed };
}

// ---- アクション座標デコード (fumenParse.ts の decodePosition 相当) ----
// type: 0=Empty,1=I,2=L,3=O,4=Z,5=T,6=J,7=S,8=Gray
// rotation: 0=Reverse,1=Right,2=Spawn,3=Left (fumen独自の並び順)
struct DecodedCoord { int x, y; };

DecodedCoord decodePosition(long long value, int type, int rotation) {
    int x = static_cast<int>(value % FIELD_W);
    int y = FIELD_H - static_cast<int>(value / FIELD_W) - 1;

    if (type == 3 && rotation == 3) { x += 1; y -= 1; }        // O, Left
    else if (type == 3 && rotation == 0) { x += 1; }           // O, Reverse
    else if (type == 3 && rotation == 2) { y -= 1; }           // O, Spawn
    else if (type == 1 && rotation == 0) { x += 1; }           // I, Reverse
    else if (type == 1 && rotation == 3) { y -= 1; }           // I, Left
    else if (type == 7 && rotation == 2) { y -= 1; }           // S, Spawn
    else if (type == 7 && rotation == 1) { x -= 1; }           // S, Right
    else if (type == 4 && rotation == 2) { y -= 1; }           // Z, Spawn
    else if (type == 4 && rotation == 3) { x += 1; }           // Z, Left

    return { x, y };
}

struct DecodedAction {
    int type;       // ミノ種 (0=Empty,1..7,8=Gray)
    int rotation;
    int x, y;
    bool rise, mirror, colorize, comment, lock;
};

DecodedAction decodeAction(long long value) {
    long long v = value;
    int type = static_cast<int>(v % 8);
    v /= 8;
    int rotation = static_cast<int>(v % 4);
    v /= 4;
    long long posVal = v % FIELD_BLOCKS;
    v /= FIELD_BLOCKS;
    bool rise = (v % 2) == 1; v /= 2;
    bool mirror = (v % 2) == 1; v /= 2;
    bool colorize = (v % 2) == 1; v /= 2;
    bool comment = (v % 2) == 1; v /= 2;
    bool lock = (v % 2) == 0;

    DecodedCoord coord = decodePosition(posVal, type, rotation);
    return { type, rotation, coord.x, coord.y, rise, mirror, colorize, comment, lock };
}

// ---- ミノの形状 (fumenParse.ts の getPieces 相当) ----
// x,yはミノ基準点からの相対座標。type: 1=I,2=L,3=O,4=Z,5=T,6=J,7=S
std::vector<std::pair<int,int>> getPieceOffsets(int type) {
    switch (type) {
        case 1: return {{0,0},{-1,0},{1,0},{2,0}};   // I
        case 5: return {{0,0},{-1,0},{1,0},{0,1}};   // T
        case 3: return {{0,0},{1,0},{0,1},{1,1}};    // O
        case 2: return {{0,0},{-1,0},{1,0},{1,1}};   // L
        case 6: return {{0,0},{-1,0},{1,0},{-1,1}};  // J
        case 7: return {{0,0},{-1,0},{0,1},{1,1}};   // S
        case 4: return {{0,0},{1,0},{0,1},{-1,1}};   // Z
        default: return {};
    }
}

std::vector<std::pair<int,int>> rotateOffsets(std::vector<std::pair<int,int>> pos, int rotation) {
    // rotation: 0=Reverse,1=Right,2=Spawn,3=Left
    switch (rotation) {
        case 2: return pos; // Spawn = そのまま
        case 3: // Left
            for (auto& p : pos) p = { -p.second, p.first };
            return pos;
        case 0: // Reverse
            for (auto& p : pos) p = { -p.first, -p.second };
            return pos;
        case 1: // Right
            for (auto& p : pos) p = { p.second, -p.first };
            return pos;
    }
    return pos;
}

bool isMinoType(int type) { return type != 0 && type != 8; }

} // namespace (無名)

// ===================================================================
// 公開API
// ===================================================================
std::vector<DecodedPage> decode(const std::string& fumenInput) {
    std::vector<DecodedPage> pages;

    try {
        std::string data = fumenInput;

        // クエリパラメータ(&以降)を除去
        auto ampPos = data.find('&');
        if (ampPos != std::string::npos) data = data.substr(0, ampPos);

        // "v115@" のようなバージョンヘッダーを探す
        std::smatch m;
        std::regex verRe("[vmd]115@");
        if (!std::regex_search(data, m, verRe)) {
            return {}; // サポート外バージョン。空を返し、呼び出し側でスキップさせる。
        }
        data = data.substr(m.position() + 5);

        // 区切り文字 '?' や空白を除去
        std::string cleaned;
        cleaned.reserve(data.size());
        for (char c : data) {
            if (c != '?' && !std::isspace(static_cast<unsigned char>(c))) cleaned += c;
        }

        Buffer buf(cleaned);
        InnerField prevField{};
        int repeatCount = -1;

        while (!buf.isEmpty()) {
            FieldDiffResult fieldResult;
            if (repeatCount > 0) {
                fieldResult = { prevField, false };
                repeatCount--;
            } else {
                fieldResult = decodeFieldDiff(prevField, buf);
                if (!fieldResult.changed) {
                    repeatCount = static_cast<int>(buf.poll(1));
                }
            }

            DecodedAction action = decodeAction(buf.poll(3));

            // コメント本体は読み飛ばす (tmpl用途では不要。ただしバッファ位置は
            // 正しく進める必要があるため、長さ分だけpollする)
            if (action.comment) {
                long long len = buf.poll(2);
                long long groups = (len + 3) / 4; // Math.ceil(len/4)
                for (long long i = 0; i < groups; ++i) buf.poll(5);
            }

            // ページの地形を確定 (ミノをロックしてからのスナップショット)
            InnerField locked = fieldResult.field;
            if (action.lock) {
                if (isMinoType(action.type)) {
                    auto offsets = rotateOffsets(getPieceOffsets(action.type), action.rotation);
                    for (auto& off : offsets) {
                        int px = action.x + off.first;
                        int py = action.y + off.second;
                        if (px >= 0 && px < FIELD_W && py >= 0 && py < FIELD_H) {
                            locked.field[px + py * FIELD_W] = action.type;
                        }
                    }
                }
                // ライン消去 (下から詰める)
                std::array<int, FIELD_BLOCKS> newField{};
                int writeY = 0;
                for (int y = 0; y < FIELD_H; ++y) {
                    bool full = true;
                    for (int x = 0; x < FIELD_W; ++x) {
                        if (locked.field[x + y * FIELD_W] == 0) { full = false; break; }
                    }
                    if (!full) {
                        for (int x = 0; x < FIELD_W; ++x) {
                            newField[x + writeY * FIELD_W] = locked.field[x + y * FIELD_W];
                        }
                        writeY++;
                    }
                }
                locked.field = newField;
            }

            DecodedPage page;
            for (int i = 0; i < FIELD_BLOCKS; ++i) {
                page.fieldRaw[i] = static_cast<uint8_t>(std::max(0, locked.field[i]));
            }
            pages.push_back(page);

            prevField = locked;
        }
    } catch (const std::exception&) {
        return {}; // 不正なfumen: 呼び出し側で「このtmplは組み込まない」と扱えるよう空で返す
    }

    return pages;
}

std::bitset<30> pageToTopNRows(const DecodedPage& page, int depth) {
    if (depth < 1 || depth > 3) depth = 3;
    std::bitset<30> bits;
    // fieldRaw: y=0が最下段、y=FIELD_H-1が最上段。上からdepth行を抽出する。
    for (int r = 0; r < depth; ++r) {
        int y = FIELD_H - depth + r; // 上からdepth行のうちr行目
        if (y < 0 || y >= FIELD_H) continue;
        for (int x = 0; x < FIELD_W; ++x) {
            if (page.fieldRaw[x + y * FIELD_W] != 0) bits.set(r * FIELD_W + x);
        }
    }
    return bits;
}

BoardBits pageToBoardBits(const DecodedPage& page) {
    BoardBits board{};
    // fumenのFIELD_H=23に対し、ゲーム側BoardBitsはBOARD_BUFFER=40行。
    // 下から詰めて対応させ、ゲーム内表示範囲(BOARD_H=20)に収まる部分を使う。
    for (int y = 0; y < FIELD_H && y < BOARD_BUFFER; ++y) {
        uint16_t row = 0;
        for (int x = 0; x < FIELD_W; ++x) {
            if (page.fieldRaw[x + y * FIELD_W] != 0) row |= (1 << x);
        }
        board[y] = row;
    }
    return board;
}

} // namespace FumenDecoder
