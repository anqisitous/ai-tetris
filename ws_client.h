// ===================================================================
// net/ws_client.h - WebSocketクライアント最小実装
//
// ゲーム本体(main_sdl3.cpp)側で使う。AIサーバーへ接続し、
// ハンドシェイクを行った後は非ブロッキングでフレームを送受信する。
// ゲームループを止めないことが最優先なので、connect後は
// setNonBlocking(true) を使う想定。
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
#include <netdb.h>
#include <optional>
#include <random>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace NetWs {

class WsClient {
public:
    // host/portへTCP接続し、WebSocketハンドシェイクを完了させる。
    bool connectTo(const std::string& host, uint16_t port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            return false;  // 今回はIPアドレス直指定のみ対応(通常は127.0.0.1)
        }

        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            return false;
        }

        int nodelay = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        return performHandshake(host, port);
    }

    bool sendBinary(const uint8_t* data, size_t len) {
        auto frame = encodeBinaryFrame(data, len, /*isClient=*/true);
        return writeAll(frame.data(), frame.size());
    }

    // 到着している全メッセージを一度に取り出す(ゲームループ1フレーム分をまとめて処理する用途)。
    std::vector<std::vector<uint8_t>> recvAllBinary() {
        std::vector<std::vector<uint8_t>> messages;

        uint8_t tmp[4096];
        while (true) {
            ssize_t n = read(fd_, tmp, sizeof(tmp));
            if (n > 0) {
                recvBuf_.insert(recvBuf_.end(), tmp, tmp + n);
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n <= 0) { closed_ = true; break; }
        }

        while (true) {
            size_t consumed = 0;
            auto frame = decodeFrame(recvBuf_.data(), recvBuf_.size(), consumed);
            if (!frame) break;
            recvBuf_.erase(recvBuf_.begin(), recvBuf_.begin() + consumed);
            if (frame->opcode == Opcode::Binary) {
                messages.push_back(std::move(frame->payload));
            } else if (frame->opcode == Opcode::Close) {
                closed_ = true;
                break;
            }
            // Ping/Pong/Textは読み捨てる
        }
        return messages;
    }

    void setNonBlocking(bool enable) {
        int flags = fcntl(fd_, F_GETFL, 0);
        if (enable) fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        else fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
    }

    bool isClosed() const { return closed_; }

    ~WsClient() {
        if (fd_ >= 0) close(fd_);
    }

private:
    int fd_ = -1;
    bool closed_ = false;
    std::vector<uint8_t> recvBuf_;

    bool writeAll(const uint8_t* data, size_t len) {
        size_t off = 0;
        while (off < len) {
            ssize_t n = write(fd_, data + off, len - off);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                return false;
            }
            off += static_cast<size_t>(n);
        }
        return true;
    }

    static std::string generateClientKey() {
        uint8_t raw[16];
        std::random_device rd;
        for (auto& b : raw) b = static_cast<uint8_t>(rd() & 0xFF);
        return NetCrypto::base64Encode(raw, sizeof(raw));
    }

    bool performHandshake(const std::string& host, uint16_t port) {
        // ハンドシェイク中だけブロッキングで読む(接続直後の一往復のみ)。
        std::string key = generateClientKey();
        std::string request =
            "GET / HTTP/1.1\r\n"
            "Host: " + host + ":" + std::to_string(port) + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + key + "\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";

        if (!writeAll(reinterpret_cast<const uint8_t*>(request.data()), request.size())) {
            return false;
        }

        std::string response;
        char buf[4096];
        while (response.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = read(fd_, buf, sizeof(buf));
            if (n <= 0) return false;
            response.append(buf, static_cast<size_t>(n));
            if (response.size() > 65536) return false;
        }

        if (response.find("101") == std::string::npos) return false;

        std::string expectedAccept = NetCrypto::computeWebSocketAccept(key);
        if (response.find(expectedAccept) == std::string::npos) return false;

        return true;
    }
};

}  // namespace NetWs
