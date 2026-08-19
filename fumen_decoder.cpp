#include "fumen_decoder.h"
#include <regex>
#include <stdexcept>

namespace FumenDecoder {

namespace {

const std::string ENCODE_TABLE =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr int TABLE_LEN = 64;

int tableIndexOf(char c) {
    auto pos = ENCODE_TABLE.find(c);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

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

struct InnerField {
    std::array<int, FIELD_BLOCKS> field{};
    std::array<int, FIELD_W> garbage{};

    int getAt(int x, int y) const {
        if (y >= 0) return field[x + y * FIELD_W];
        return garbage[x];
    }
    void addAt(int x, int y, int diff) {
        if (y >= 0) field[x + y * FIELD_W] += diff;
        else garbage[x] += diff;
    }
};

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

struct DecodedCoord { int x, y; };

DecodedCoord decodePosition(long long value, int type, int rotation) {
    int x = static_cast<int>(value % FIELD_W);
    int y = FIELD_H - static_cast<int>(value / FIELD_W) - 1;

    if (type == 3 && rotation == 3) { x += 1; y -= 1; }
    else if (type == 3 && rotation == 0) { x += 1; }
    else if (type == 3 && rotation == 2) { y -= 1; }
    else if (type == 1 && rotation == 0) { x += 1; }
    else if (type == 1 && rotation == 3) { y -= 1; }
    else if (type == 7 && rotation == 2) { y -= 1; }
    else if (type == 7 && rotation == 1) { x -= 1; }
    else if (type == 4 && rotation == 2) { y -= 1; }
    else if (type == 4 && rotation == 3) { x += 1; }

    return { x, y };
}

struct DecodedAction {
    int type;
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

std::vector<std::pair<int,int>> getPieceOffsets(int type) {
    switch (type) {
        case 1: return {{0,0},{-1,0},{1,0},{2,0}};
        case 5: return {{0,0},{-1,0},{1,0},{0,1}};
        case 3: return {{0,0},{1,0},{0,1},{1,1}};
        case 2: return {{0,0},{-1,0},{1,0},{1,1}};
        case 6: return {{0,0},{-1,0},{1,0},{-1,1}};
        case 7: return {{0,0},{-1,0},{0,1},{1,1}};
        case 4: return {{0,0},{1,0},{0,1},{-1,1}};
        default: return {};
    }
}

std::vector<std::pair<int,int>> rotateOffsets(std::vector<std::pair<int,int>> pos, int rotation) {
    switch (rotation) {
        case 2: return pos;
        case 3:
            for (auto& p : pos) p = { -p.second, p.first };
            return pos;
        case 0:
            for (auto& p : pos) p = { -p.first, -p.second };
            return pos;
        case 1:
            for (auto& p : pos) p = { p.second, -p.first };
            return pos;
    }
    return pos;
}

bool isMinoType(int type) { return type != 0 && type != 8; }

}

std::vector<DecodedPage> decode(const std::string& fumenInput) {
    std::vector<DecodedPage> pages;

    try {
        std::string data = fumenInput;

        auto ampPos = data.find('&');
        if (ampPos != std::string::npos) data = data.substr(0, ampPos);

        std::smatch m;
        std::regex verRe("[vmd]115@");
        if (!std::regex_search(data, m, verRe)) {
            return {};
        }
        data = data.substr(m.position() + 5);

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

            if (action.comment) {
                long long len = buf.poll(2);
                long long groups = (len + 3) / 4;
                for (long long i = 0; i < groups; ++i) buf.poll(5);
            }

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
    } catch (const std::exception& ex) {
#ifdef FUMEN_DEBUG_THROW
        throw;
#else
        (void)ex;
        return {};
#endif
    }

    return pages;
}

std::bitset<30> pageToTopNRows(const DecodedPage& page, int depth) {
    if (depth < 1 || depth > 3) depth = 3;
    std::bitset<30> bits;
    for (int r = 0; r < depth; ++r) {
        int y = FIELD_H - depth + r;
        if (y < 0 || y >= FIELD_H) continue;
        for (int x = 0; x < FIELD_W; ++x) {
            if (page.fieldRaw[x + y * FIELD_W] != 0) bits.set(r * FIELD_W + x);
        }
    }
    return bits;
}

BoardBits pageToBoardBits(const DecodedPage& page) {
    BoardBits board{};
    for (int y = 0; y < FIELD_H && y < BOARD_BUFFER; ++y) {
        uint16_t row = 0;
        for (int x = 0; x < FIELD_W; ++x) {
            if (page.fieldRaw[x + y * FIELD_W] != 0) row |= (1 << x);
        }
        board[y] = row;
    }
    return board;
}

}  // namespace FumenDecoder
