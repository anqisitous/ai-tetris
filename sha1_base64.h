// ===================================================================
// net/sha1_base64.h - WebSocketハンドシェイク用 SHA1 + Base64 最小実装
//
// RFC6455のハンドシェイクは
//   base64( SHA1( Sec-WebSocket-Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" ) )
// を計算できればよく、これ以外の暗号処理は不要。外部ライブラリ(OpenSSL等)への
// 依存を避け、macOSへのbrewインストール工程を増やさないために自前で持つ。
// ===================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <array>

namespace NetCrypto {

// ---- SHA1 (RFC3174準拠、20バイトダイジェストを返す) ----
inline std::array<uint8_t, 20> sha1(const std::string& input) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
             h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::string msg = input;
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
    msg += static_cast<char>(0x80);
    while (msg.size() % 64 != 56) msg += static_cast<char>(0x00);
    for (int i = 7; i >= 0; --i) {
        msg += static_cast<char>((bitLen >> (i * 8)) & 0xFF);
    }

    for (size_t chunkStart = 0; chunkStart < msg.size(); chunkStart += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const uint8_t*>(msg.data() + chunkStart + i * 4);
            w[i] = (static_cast<uint32_t>(p[0]) << 24) |
                   (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8) |
                   static_cast<uint32_t>(p[3]);
        }
        for (int i = 16; i < 80; ++i) {
            uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (v << 1) | (v >> 31);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }

            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<uint8_t, 20> digest;
    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest[i * 4 + 0] = (hs[i] >> 24) & 0xFF;
        digest[i * 4 + 1] = (hs[i] >> 16) & 0xFF;
        digest[i * 4 + 2] = (hs[i] >> 8) & 0xFF;
        digest[i * 4 + 3] = hs[i] & 0xFF;
    }
    return digest;
}

// ---- Base64エンコード ----
inline std::string base64Encode(const uint8_t* data, size_t len) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += table[(v >> 18) & 0x3F];
        out += table[(v >> 12) & 0x3F];
        out += table[(v >> 6) & 0x3F];
        out += table[v & 0x3F];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = data[i] << 16;
        out += table[(v >> 18) & 0x3F];
        out += table[(v >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
        out += table[(v >> 18) & 0x3F];
        out += table[(v >> 12) & 0x3F];
        out += table[(v >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

// ---- WebSocket Accept鍵の計算 ----
inline std::string computeWebSocketAccept(const std::string& clientKey) {
    static const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    auto digest = sha1(clientKey + GUID);
    return base64Encode(digest.data(), digest.size());
}

}  // namespace NetCrypto
