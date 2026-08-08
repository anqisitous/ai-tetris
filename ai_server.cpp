// ===================================================================
// ai_server.cpp - Oracle AIサーバー
//
// 設計方針(詳細はprotocol.hのコメント参照):
//   - サーバーはPlayerStateを内部に保持するが、これは「記憶」ではなく
//     直近に受け取ったBoardStateFrameの単純なキャッシュであり、
//     次のBoardStateFrameが届くたびに丸ごと置き換わる(全置換)。
//   - AI側の盤面は着地(一手)ごとにBoardStateFrameとして届く。
//     届くたびにbeamSearchを1回実行し、結果をAIActionFrameとして返す。
//   - InputEventFrame(人間側P1の生キー操作)は非同期ストリームとして
//     受信するが、判断には使わない。今後の拡張(相手を見た評価)に
//     備えて受信だけはしておく。
//   - ゲームループはこのサーバーからの応答を待たずに進行する前提。
//     (AI vs AiモードではAI側の着地確定もready待ちをしないが、
//      それはローカル側main_sdl3.cppの制御であり、このサーバー自体は
//      モードを意識せず、来たBoardStateFrameに対して淡々と
//      beamSearchを実行するだけでよい)
// ===================================================================

#include "ws_server.h"
#include "protocol.h"
#include "game_engine.h"
#include "ai_core.h"

#include <iostream>
#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>

volatile sig_atomic_t running = 1;

void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) running = 0;
}

// ---- テンプレート読み込み(既存のまま) ----
void loadAllTemplates(TemplateLibrary& lib) {
    const char* cachePath = "terrain_cache.dat";
    const char* fumenListPath = "fumen_list.txt";
    const char* tmplPath = "templates.tmpl";

    int loadedFromCache = TemplateLoader::loadTerrainCache(cachePath, lib);
    if (loadedFromCache > 0) {
        printf("[templates] terrain_cache.dat から %d 件の地形を読み込みました\n", loadedFromCache);
    } else {
        std::ifstream check(fumenListPath);
        if (check.is_open()) {
            check.close();
            int loadedFromFumen = TemplateLoader::loadFumenListFile(fumenListPath, lib, 3);
            printf("[templates] fumen_list.txt から %d 件の地形を読み込みました\n", loadedFromFumen);
            if (loadedFromFumen > 0) {
                bool built = TemplateLoader::buildTerrainCache(fumenListPath, cachePath, 3);
                if (built) {
                    printf("[templates] terrain_cache.dat を生成しました\n");
                }
            }
        }
    }

    std::ifstream tmplCheck(tmplPath);
    if (tmplCheck.is_open()) {
        tmplCheck.close();
        int loadedFromTmpl = TemplateLoader::loadTmplFile(tmplPath, lib);
        printf("[templates] templates.tmpl から %d 件の地形を読み込みました\n", loadedFromTmpl);
    }
}

// ---- BoardStateFrame -> PlayerState への復元 ----
// BoardStateFrameに含まれない項目(score, level, gameOver等)は
// beamSearchの意思決定に不要なため、デフォルト値のまま残す。
void applyBoardStateFrame(const NetProtocol::BoardStateFrame& f, PlayerState& ps) {
    static_assert(NetProtocol::BoardStateFrame::BOARD_ROWS == BOARD_BUFFER,
                  "BoardStateFrame's row count must match BOARD_BUFFER");

    for (size_t i = 0; i < BOARD_BUFFER; ++i) {
        ps.board[i] = f.boardRows[i];
    }
    ps.curType = static_cast<PType>(f.curType);
    ps.curRot = f.curRot;
    ps.curX = f.curX;
    ps.curY = f.curY;

    ps.next.clear();
    for (size_t i = 0; i < NetProtocol::BoardStateFrame::NEXT_COUNT; ++i) {
        ps.next.push_back(static_cast<PType>(f.nextTypes[i]));
    }
    ps.hold = static_cast<PType>(f.holdType);
    ps.canHold = (f.canHold != 0);
    ps.holdUsed = false;
    ps.combo = f.combo;
    ps.btb = f.btb;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "[Oracle Server] Starting..." << std::endl;

    int port = 8080;
    const char* envPort = getenv("PORT");
    if (envPort) port = std::atoi(envPort);
    std::cout << "[Oracle Server] Port: " << port << std::endl;

    NetWs::WsServer server;
    if (!server.listenAndAccept(static_cast<uint16_t>(port))) {
        std::cerr << "[Oracle Server] Failed to listen on port " << port << std::endl;
        return 1;
    }
    server.setNonBlocking(true);
    std::cout << "[Oracle Server] Client connected!" << std::endl;

    // AIState(テンプレート・パターンメモリ)はセッションを通じて保持する。
    // PlayerStateは毎回のBoardStateFrameで全置換されるだけのキャッシュ。
    AIState aiState;
    TemplateLibrary templateLib;
    loadAllTemplates(templateLib);
    aiState.templateLib = &templateLib;
    aiState.patternMemory = PatternMemory();

    PlayerState cachedPlayer;
    cachedPlayer.init(123);

    uint32_t lastBoardSeq = 0;
    uint32_t lastInputSeq = 0;

    while (running && !server.isClosed()) {
        auto msg = server.recvBinary();
        if (!msg) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (msg->size() >= 1 && (*msg)[0] == NetProtocol::MAGIC_BOARD_STATE) {
            if (msg->size() < NetProtocol::BoardStateFrame::WIRE_SIZE) continue;

            NetProtocol::BoardStateFrame boardFrame =
                NetProtocol::BoardStateFrame::fromBytes(msg->data());

            // 古い(順序が入れ替わった)着地イベントは無視する。
            if (boardFrame.seq != 0 && boardFrame.seq <= lastBoardSeq &&
                lastBoardSeq != 0) {
                continue;
            }
            lastBoardSeq = boardFrame.seq;

            // 一手ごとの盤面同期: 受け取った盤面で丸ごと置き換える。
            applyBoardStateFrame(boardFrame, cachedPlayer);

            // このBoardStateFrameに対してbeamSearchを1回実行する。
            BeamNode node = beamSearch(aiState, cachedPlayer, 20, 3);
            AIAction act = makeActionFromBeam(node);

            NetProtocol::AIActionFrame actionFrame;
            actionFrame.magic = NetProtocol::MAGIC_AI_ACTION;
            actionFrame.seq = boardFrame.seq;  // どの着地イベントに対する応答かを紐づける
            actionFrame.targetX = act.targetX;
            actionFrame.targetRot = act.targetRot;
            actionFrame.shouldHold = act.shouldHold ? 1 : 0;
            actionFrame.shouldDrop = act.shouldDrop ? 1 : 0;
            actionFrame.ready = act.ready ? 1 : 0;

            uint8_t buffer[NetProtocol::AIActionFrame::WIRE_SIZE];
            actionFrame.toBytes(buffer);
            server.sendBinary(buffer, NetProtocol::AIActionFrame::WIRE_SIZE);

        } else if (msg->size() >= 1 && (*msg)[0] == NetProtocol::MAGIC_INPUT_EVENT) {
            if (msg->size() < NetProtocol::InputEventFrame::WIRE_SIZE) continue;
            NetProtocol::InputEventFrame inputFrame =
                NetProtocol::InputEventFrame::fromBytes(msg->data());
            // 現状は判断に使わない。将来、相手の入力状況を評価に
            // 組み込む拡張(neuronの待ち合わせ判断等)のための受け口として
            // 最新seqだけ記録しておく。
            if (inputFrame.seq > lastInputSeq) lastInputSeq = inputFrame.seq;
        }
    }

    std::cout << "[Oracle Server] Shutting down..." << std::endl;
    return 0;
}
