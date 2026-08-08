// ===================================================================
// net/protocol.h - ゲーム本体 <-> AIサーバー間の固定長バイナリプロトコル
//
// 設計方針:
//   - 盤面状態の真実は常にゲーム本体(描画側)が保持する。
//   - 人間側プレイヤーのキーボード入力イベントは、常時ストリーミングで
//     AIサーバーへ送り続ける(InputEventFrame)。判断には使わないが、
//     セッションの生存確認・タイムスタンプ同期を兼ねる。
//   - AI側の盤面は「AIが一手打つごと」、すなわちミノが着地して
//     盤面が確定するたびに、その最新盤面をまるごとBoardStateFrameとして
//     AIサーバーへ送る。AIサーバーは内部にPlayerStateを保持し、
//     受け取ったBoardStateFrameで即座に丸ごと置き換える
//     (差分適用ではなく全置換。取りこぼし・順序ズレに強い)。
//   - AIサーバーはBoardStateFrameを受け取るたびにbeamSearchを実行し、
//     結果が出来次第AIActionFrameを送り返す。
//   - 通信は同期リクエスト/レスポンスではなく非同期ストリームとして扱う。
//     ゲームループはAIActionFrameの到着を待たずに進行する。
//     ただし「AI側ピースの移動・ドロップの確定」だけは、直近の
//     AIActionFrame.readyが立つまでexecuteAI側で保留にする
//     (人間側プレイヤーの操作性やローカルの時間進行は一切止めない)。
//
// 全フレームはリトルエンディアン固定長。ネットワークバイトオーダー変換は
// 行わない(同一ホスト内および同一LAN内のTCP/WebSocket通信を前提とする)。
// ===================================================================
#pragma once

#include <cstdint>
#include <cstring>

namespace NetProtocol {

// ---- フレーム境界検出用マジックバイト ----
constexpr uint8_t MAGIC_INPUT_EVENT  = 0xA1;   // ゲーム本体 -> AIサーバー
constexpr uint8_t MAGIC_AI_ACTION    = 0xA2;   // AIサーバー -> ゲーム本体
constexpr uint8_t MAGIC_BOARD_STATE  = 0xA3;   // ゲーム本体 -> AIサーバー(AI一手ごとの盤面同期)

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

// ---- ゲーム本体 -> AIサーバー: AIが一手打つ(着地する)たびの盤面全体同期 ----
// BOARD_BUFFER=40行 x uint16_t = 80バイト固定。
// 差分は送らず毎回まるごと送る全置換方式なので、取りこぼしても
// 次の着地イベントで自動的に復旧する。
// ネクストは探索深度(現状3)より十分余裕を持たせて5手分を送る
// (PlayerState::next は常に5個保持される設計に合わせている)。
struct BoardStateFrame {
    static constexpr size_t BOARD_ROWS = 40;
    static constexpr size_t NEXT_COUNT = 5;

    uint8_t  magic = MAGIC_BOARD_STATE;
    uint32_t seq = 0;                       // 着地イベントの単調増加シーケンス番号
    uint16_t boardRows[BOARD_ROWS] = {};    // BoardBits をそのまま列挙
    uint8_t  curType = 0;                   // 現在のミノ種類(PType)
    uint8_t  curRot = 0;
    int32_t  curX = 0;
    int32_t  curY = 0;
    uint8_t  nextTypes[NEXT_COUNT] = {};    // ネクスト5手分(PType)
    uint8_t  holdType = 0;                  // ホールド中のミノ種類(PType)
    uint8_t  canHold = 1;                   // 0/1
    int32_t  combo = 0;
    int32_t  btb = 0;

    static constexpr size_t WIRE_SIZE =
        1 + 4 + (BOARD_ROWS * 2) + 1 + 1 + 4 + 4 + NEXT_COUNT + 1 + 1 + 4 + 4;
    // = 1+4+80+1+1+4+4+5+1+1+4+4 = 110

    void toBytes(uint8_t* out) const {
        size_t off = 0;
        out[off++] = magic;
        std::memcpy(out + off, &seq, 4); off += 4;
        std::memcpy(out + off, boardRows, BOARD_ROWS * 2); off += BOARD_ROWS * 2;
        out[off++] = curType;
        out[off++] = curRot;
        std::memcpy(out + off, &curX, 4); off += 4;
        std::memcpy(out + off, &curY, 4); off += 4;
        std::memcpy(out + off, nextTypes, NEXT_COUNT); off += NEXT_COUNT;
        out[off++] = holdType;
        out[off++] = canHold;
        std::memcpy(out + off, &combo, 4); off += 4;
        std::memcpy(out + off, &btb, 4); off += 4;
    }

    static BoardStateFrame fromBytes(const uint8_t* in) {
        BoardStateFrame f;
        size_t off = 0;
        f.magic = in[off++];
        std::memcpy(&f.seq, in + off, 4); off += 4;
        std::memcpy(f.boardRows, in + off, BOARD_ROWS * 2); off += BOARD_ROWS * 2;
        f.curType = in[off++];
        f.curRot = in[off++];
        std::memcpy(&f.curX, in + off, 4); off += 4;
        std::memcpy(&f.curY, in + off, 4); off += 4;
        std::memcpy(f.nextTypes, in + off, NEXT_COUNT); off += NEXT_COUNT;
        f.holdType = in[off++];
        f.canHold = in[off++];
        std::memcpy(&f.combo, in + off, 4); off += 4;
        std::memcpy(&f.btb, in + off, 4); off += 4;
        return f;
    }
};
#pragma pack(pop)

}  // namespace NetProtocol
