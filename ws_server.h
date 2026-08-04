// ===================================================================
// net/ws_server.h - WebSocketサーバー最小実装
//
// AIサーバープロセス側で使う。1接続(ゲーム本体からの接続)のみを
// 前提とした単純なブロッキングI/O実装。マルチ接続や非同期I/Oは
// 今回のスコープ(ゲーム本体1つとAIサーバー1つが1対1で通信する)には
// 不要と判断し、対応しない。
// ===================================================================
#pragma once

#include "ws_frame.h"
#include "sha1_base64.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace NetWs {

class WsServer {
public:
    // 指定ポートでlistenし、1接続分のハンドシェイクが完了するまでブロックする。
    // 成功したらtrueを返す。
    bool listenAndAccept(uint16_t port) {
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) return false;

        int opt = 1;
        setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 同一ホストのみ受け付ける
        addr.sin_port = htons(port);

        if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            return false;
        }
        if (listen(listenFd_, 1) < 0) {
            return false;
        }

        clientFd_ = accept(listenFd_, nullptr, nullptr);
        if (clientFd_ < 0) return false;

        int nodelay = 1;
        setsockopt(clientFd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        return performHandshake();
    }

    // バイナリメッセージ1件を送信する(サーバー->クライアントなのでマスクしない)。
    bool sendBinary(const uint8_t* data, size_t len) {
        auto frame = encodeBinaryFrame(data, len, /*isClient=*/false);
        return writeAll(frame.data(), frame.size());
    }

    // 受信バッファにデータを補充しつつ、1メッセージ分デコードできたら返す。
    // ノンブロッキング的に使いたい場合は事前にsetNonBlocking(true)を呼ぶ。
    // メッセージが来ていなければ std::nullopt。
    std::optional<std::vector<uint8_t>> recvBinary() {
        while (true) {
            size_t consumed = 0;
            auto frame = decodeFrame(recvBuf_.data(), recvBuf_.size(), consumed);
            if (frame) {
                recvBuf_.erase(recvBuf_.begin(), recvBuf_.begin() + consumed);
                if (frame->opcode == Opcode::Binary) {
                    return frame->payload;
                }
                if (frame->opcode == Opcode::Close) {
                    closed_ = true;
                    return std::nullopt;
                }
                // Ping/Pong/Textは読み捨てて次のフレームを待つ
                continue;
            }

            uint8_t tmp[4096];
            ssize_t n = read(clientFd_, tmp, sizeof(tmp));
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    return std::nullopt;  // ノンブロッキング時: 今はデータなし
                }
                closed_ = true;
                return std::nullopt;
            }
            recvBuf_.insert(recvBuf_.end(), tmp, tmp + n);
        }
    }

    void setNonBlocking(bool enable) {
        int flags = fcntl(clientFd_, F_GETFL, 0);
        if (enable) fcntl(clientFd_, F_SETFL, flags | O_NONBLOCK);
        else fcntl(clientFd_, F_SETFL, flags & ~O_NONBLOCK);
    }

    bool isClosed() const { return closed_; }

    ~WsServer() {
        if (clientFd_ >= 0) close(clientFd_);
        if (listenFd_ >= 0) close(listenFd_);
    }

private:
    int listenFd_ = -1;
    int clientFd_ = -1;
    bool closed_ = false;
    std::vector<uint8_t> recvBuf_;

    bool writeAll(const uint8_t* data, size_t len) {
        size_t off = 0;
        while (off < len) {
            ssize_t n = write(clientFd_, data + off, len - off);
            if (n <= 0) return false;
            off += static_cast<size_t>(n);
        }
        return true;
    }

    // HTTP Upgradeリクエストを読み、Sec-WebSocket-Keyを取り出して
    // 101 Switching Protocolsを返す。
    bool performHandshake() {
        std::string request;
        char buf[4096];
        // "\r\n\r\n" が来るまで読み続ける(ヘッダは複数readにまたがりうる)
        while (request.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = read(clientFd_, buf, sizeof(buf));
            if (n <= 0) return false;
            request.append(buf, static_cast<size_t>(n));
            if (request.size() > 65536) return false;  // 異常に長いヘッダは拒否
        }

        std::string key = extractHeaderValue(request, "Sec-WebSocket-Key");
        if (key.empty()) return false;

        std::string accept = NetCrypto::computeWebSocketAccept(key);

        std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

        return writeAll(reinterpret_cast<const uint8_t*>(response.data()), response.size());
    }

    static std::string extractHeaderValue(const std::string& request, const std::string& header) {
        std::string needle = header + ":";
        size_t pos = request.find(needle);
        if (pos == std::string::npos) return "";
        pos += needle.size();
        size_t end = request.find("\r\n", pos);
        if (end == std::string::npos) return "";
        std::string value = request.substr(pos, end - pos);
        // 前後の空白を除去
        size_t start = value.find_first_not_of(" \t");
        size_t last = value.find_last_not_of(" \t");
        if (start == std::string::npos) return "";
        return value.substr(start, last - start + 1);
    }
};

}  // namespace NetWs
