// ===================================================================
// net/protocol.h - ゲーム本体 <-> AIサーバー間の固定長バイナリプロトコル
//
// 設計方針:
//   - 盤面状態の真実は常にゲーム本体(描画側)が保持する。
//   - AIサーバーへは「人間側プレイヤーのキーボード入力イベント」のみを
//     常時ストリーミングで送る。盤面そのものは送らない。
//   - AIサーバーは受け取った入力イベント履歴を自前のgame_engineロジックで
//     再生し、内部状態として盤面を復元しながらAI側の一手を判断する。
//   - 通信は1手ごとの同期リクエスト/レスポンスではなく、双方向の
//     非同期ストリームとして扱う。ゲーム側は入力が発生するたびに
//     InputEventFrameを送りっぱなしにし、AIサーバーは判断が出来次第
//     AIActionFrameを送り返す。ゲームループはAIActionFrameの到着を
//     待たずに進行する(次に来たフレームを都度適用するだけ)。
//
// 全フレームはリトルエンディアン固定長。ネットワークバイトオーダー変換は
// 行わない(同一ホスト内のTCP/WebSocketループバック通信を前提とする)。
// ===================================================================
#pragma once

#include <cstdint>
#include <cstring>

namespace NetProtocol {

// ---- フレーム境界検出用マジックバイト ----
constexpr uint8_t MAGIC_INPUT_EVENT = 0xA1;   // ゲーム本体 -> AIサーバー
constexpr uint8_t MAGIC_AI_ACTION   = 0xA2;   // AIサーバー -> ゲーム本体

// ---- キーイベント種別 ----
// main_sdl3.cpp の p1 操作キーに1:1対応させる。
enum class KeyCode : uint8_t {
    LeftDown      = 0,  // LEFTキー押下(移動開始)
    RightDown     = 1,  // RIGHTキー押下(移動開始)
    LeftUp        = 2,  // LEFTキー離す
    RightUp       = 3,  // RIGHTキー離す
    RotateCW      = 4,  // Xキー(時計回り)
    RotateCCW     = 5,  // Zキー(反時計回り)
    HardDrop      = 6,  // UPキー
    Hold          = 7,  // Cキー
    SoftDropOn    = 8,  // DOWNキー押下
    SoftDropOff   = 9,  // DOWNキー解放
};

// ---- ゲーム本体 -> AIサーバー: 入力イベント1件 ----
// 合計10バイト固定。
#pragma pack(push, 1)
struct InputEventFrame {
    uint8_t  magic = MAGIC_INPUT_EVENT;
    uint32_t seq = 0;           // 単調増加シーケンス番号(順序保証・重複検出用)
    uint8_t  keyCode = 0;       // KeyCode
    uint32_t timestampMs = 0;   // セッション開始からの相対時刻(ms)

    static constexpr size_t WIRE_SIZE = 1 + 4 + 1 + 4;  // = 10

    void toBytes(uint8_t* out) const {
        size_t off = 0;
        out[off++] = magic;
        std::memcpy(out + off, &seq, 4); off += 4;
        out[off++] = keyCode;
        std::memcpy(out + off, &timestampMs, 4); off += 4;
    }

    static InputEventFrame fromBytes(const uint8_t* in) {
        InputEventFrame f;
        size_t off = 0;
        f.magic = in[off++];
        std::memcpy(&f.seq, in + off, 4); off += 4;
        f.keyCode = in[off++];
        std::memcpy(&f.timestampMs, in + off, 4); off += 4;
        return f;
    }
};

// ---- AIサーバー -> ゲーム本体: AI側の一手判断 ----
// 合計16バイト固定。AIAction(ai_core.h)の意思決定に必要な最小部分のみ。
struct AIActionFrame {
    uint8_t  magic = MAGIC_AI_ACTION;
    uint32_t seq = 0;            // どの内部判断サイクルに対応するか(単調増加)
    int32_t  targetX = 0;
    int32_t  targetRot = 0;
    uint8_t  shouldHold = 0;     // 0/1
    uint8_t  shouldDrop = 0;     // 0/1
    uint8_t  ready = 0;          // 0/1: 判断がまだ準備できていない場合は0

    static constexpr size_t WIRE_SIZE = 1 + 4 + 4 + 4 + 1 + 1 + 1;  // = 16

    void toBytes(uint8_t* out) const {
        size_t off = 0;
        out[off++] = magic;
        std::memcpy(out + off, &seq, 4); off += 4;
        std::memcpy(out + off, &targetX, 4); off += 4;
        std::memcpy(out + off, &targetRot, 4); off += 4;
        out[off++] = shouldHold;
        out[off++] = shouldDrop;
        out[off++] = ready;
    }

    static AIActionFrame fromBytes(const uint8_t* in) {
        AIActionFrame f;
        size_t off = 0;
        f.magic = in[off++];
        std::memcpy(&f.seq, in + off, 4); off += 4;
        std::memcpy(&f.targetX, in + off, 4); off += 4;
        std::memcpy(&f.targetRot, in + off, 4); off += 4;
        f.shouldHold = in[off++];
        f.shouldDrop = in[off++];
        f.ready = in[off++];
        return f;
    }
};
#pragma pack(pop)

}  // namespace NetProtocol
