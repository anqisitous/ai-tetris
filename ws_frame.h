// ===================================================================
// net/ws_frame.h - WebSocketフレーミング最小実装 (RFC6455)
//
// 対応範囲を意図的に絞る:
//   - opcode: バイナリ(0x2)のみを送受信対象とする(本プロトコルは
//     固定長バイナリフレームしかやり取りしないため)。ping/pong/closeは
//     受信側で最低限捌けるようにする。
//   - ペイロード長: 送るフレームは全て125バイト以下(protocol.hの
//     フレームは最大16バイト)なので、拡張長(126/127)のエンコードは
//     受信側のみ対応し、送信側は必要としない。
//   - fragmentation(FINビットが0の継続フレーム)には対応しない。
//     1メッセージ=1フレームで完結させる。
// ===================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <optional>
#include <random>

namespace NetWs {

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text         = 0x1,
    Binary       = 0x2,
    Close        = 0x8,
    Ping         = 0x9,
    Pong         = 0xA,
};

struct DecodedFrame {
    Opcode opcode;
    std::vector<uint8_t> payload;
};

// ---- 送信用: バイナリフレームを組み立てる ----
// isClient=true の場合はRFC6455の要求通りペイロードをマスクする
// (サーバーからクライアントへの送信はマスク不要)。
inline std::vector<uint8_t> encodeBinaryFrame(const uint8_t* data, size_t len, bool isClient) {
    std::vector<uint8_t> out;
    out.reserve(len + 14);

    out.push_back(0x80 | static_cast<uint8_t>(Opcode::Binary));  // FIN=1, opcode=Binary

    uint8_t maskBit = isClient ? 0x80 : 0x00;
    if (len <= 125) {
        out.push_back(maskBit | static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        out.push_back(maskBit | 126);
        out.push_back((len >> 8) & 0xFF);
        out.push_back(len & 0xFF);
    } else {
        out.push_back(maskBit | 127);
        for (int i = 7; i >= 0; --i) out.push_back((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF);
    }

    if (isClient) {
        uint8_t maskKey[4];
        std::random_device rd;
        for (auto& b : maskKey) b = static_cast<uint8_t>(rd() & 0xFF);
        out.insert(out.end(), maskKey, maskKey + 4);
        for (size_t i = 0; i < len; ++i) {
            out.push_back(data[i] ^ maskKey[i % 4]);
        }
    } else {
        out.insert(out.end(), data, data + len);
    }
    return out;
}

// ---- 受信用: バッファから1フレーム分をデコードする ----
// バッファにフレーム全体がまだ揃っていない場合は std::nullopt を返し、
// 呼び出し側は追加受信してから再試行する。デコードに成功した分は
// consumedBytes に書き込み、呼び出し側でバッファから取り除く。
inline std::optional<DecodedFrame> decodeFrame(const uint8_t* buf, size_t bufLen, size_t& consumedBytes) {
    if (bufLen < 2) return std::nullopt;

    uint8_t byte0 = buf[0];
    uint8_t byte1 = buf[1];
    Opcode opcode = static_cast<Opcode>(byte0 & 0x0F);
    bool masked = (byte1 & 0x80) != 0;
    uint64_t len = byte1 & 0x7F;

    size_t off = 2;
    if (len == 126) {
        if (bufLen < off + 2) return std::nullopt;
        len = (static_cast<uint64_t>(buf[off]) << 8) | buf[off + 1];
        off += 2;
    } else if (len == 127) {
        if (bufLen < off + 8) return std::nullopt;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | buf[off + i];
        off += 8;
    }

    uint8_t maskKey[4] = {0, 0, 0, 0};
    if (masked) {
        if (bufLen < off + 4) return std::nullopt;
        std::memcpy(maskKey, buf + off, 4);
        off += 4;
    }

    if (bufLen < off + len) return std::nullopt;  // ペイロードがまだ全部届いていない

    DecodedFrame frame;
    frame.opcode = opcode;
    frame.payload.resize(len);
    for (uint64_t i = 0; i < len; ++i) {
        uint8_t b = buf[off + i];
        if (masked) b ^= maskKey[i % 4];
        frame.payload[i] = b;
    }

    consumedBytes = off + len;
    return frame;
}

}  // namespace NetWs
