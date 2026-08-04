// ===================================================================
// ai_server_single.cpp - 単一ファイル版AIサーバー
// 全てのヘッダをインクルードしてビルド
// ===================================================================

// ---- すべてのヘッダをインクルード ----
#include "ws_server.h"
#include "protocol.h"
#include "game_engine.h"
#include "ai_core.h"

// ---- game_engine.cppの実装をここにコピー ----
// （game_engine.cppの全内容をここに貼り付け）

// ---- ai_server.cppのメイン関数 ----
#include <iostream>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <chrono>
#include <signal.h>
#include <cstring>
#include <fstream>

volatile sig_atomic_t running = 1;

void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) running = 0;
}

// ---- テンプレート読み込み ----
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

// ---- クライアント状態 ----
struct ClientGameState {
    PlayerState player;
    AIState aiState;
    TemplateLibrary templateLib;
    std::atomic<uint32_t> lastSeq{0};
    std::queue<NetProtocol::InputEventFrame> inputQueue;
    std::mutex queueMutex;
    bool initialized = false;
};

// ---- キー入力を適用 ----
void applyKeyToPlayer(PlayerState& ps, NetProtocol::KeyCode key) {
    const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
    switch (key) {
        case NetProtocol::KeyCode::LeftDown:
            if (!IsCollision(ps.board, shape, ps.curX - 1, ps.curY)) ps.curX--;
            break;
        case NetProtocol::KeyCode::RightDown:
            if (!IsCollision(ps.board, shape, ps.curX + 1, ps.curY)) ps.curX++;
            break;
        case NetProtocol::KeyCode::RotateCW:
            if (!IsCollision(ps.board, 
                SHAPES[(int)ps.curType][(ps.curRot + 1) % 4], 
                ps.curX, ps.curY)) ps.curRot = (ps.curRot + 1) % 4;
            break;
        case NetProtocol::KeyCode::RotateCCW:
            if (!IsCollision(ps.board,
                SHAPES[(int)ps.curType][(ps.curRot + 3) % 4],
                ps.curX, ps.curY)) ps.curRot = (ps.curRot + 3) % 4;
            break;
        case NetProtocol::KeyCode::HardDrop:
            hardDropPlayer(ps);
            break;
        case NetProtocol::KeyCode::Hold:
            holdPiece(ps);
            break;
        case NetProtocol::KeyCode::SoftDropOn:
            ps.softDrop = true;
            break;
        case NetProtocol::KeyCode::SoftDropOff:
            ps.softDrop = false;
            break;
        default:
            break;
    }
}

// ---- AI思考 ----
AIAction thinkAI(AIState& state, PlayerState& ps, int beamWidth, int depth) {
    BeamNode node = beamSearch(state, ps, beamWidth, depth);
    return makeActionFromBeam(node);
}

// ---- メイン ----
int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "[AI Server] Starting on Railway..." << std::endl;
    
    // RailwayのPORT環境変数を使用
    int port = 8080;
    const char* envPort = getenv("PORT");
    if (envPort) port = std::atoi(envPort);
    std::cout << "[AI Server] Port: " << port << std::endl;
    
    NetWs::WsServer server;
    if (!server.listenAndAccept(static_cast<uint16_t>(port))) {
        std::cerr << "[AI Server] Failed to listen on port " << port << std::endl;
        return 1;
    }
    
    server.setNonBlocking(true);
    std::cout << "[AI Server] Client connected!" << std::endl;
    
    ClientGameState state;
    state.player.init(123);
    state.aiState.templateLib = &state.templateLib;
    state.aiState.patternMemory = PatternMemory();
    loadAllTemplates(state.templateLib);
    state.initialized = true;
    state.player.softDrop = false;
    
    auto lastTime = std::chrono::steady_clock::now();
    double thinkTimer = 0;
    double thinkInterval = 0.10;
    double softDropSpeed = 0.03;
    
    while (running && !server.isClosed()) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        thinkTimer += dt;
        
        // 入力キュー処理
        std::vector<NetProtocol::InputEventFrame> inputs;
        {
            std::lock_guard<std::mutex> lock(state.queueMutex);
            while (!state.inputQueue.empty()) {
                inputs.push_back(state.inputQueue.front());
                state.inputQueue.pop();
            }
        }
        
        for (auto& input : inputs) {
            if (input.seq > state.lastSeq) {
                state.lastSeq = input.seq;
                applyKeyToPlayer(state.player, 
                    static_cast<NetProtocol::KeyCode>(input.keyCode));
            }
        }
        
        // 重力落下
        if (!state.player.gameOver) {
            stepPlayer(state.player, dt, softDropSpeed);
        }
        
        // AI思考
        if (thinkTimer >= thinkInterval && !state.player.gameOver) {
            thinkTimer = 0;
            AIAction act = thinkAI(state.aiState, state.player, 20, 3);
            
            NetProtocol::AIActionFrame frame;
            frame.magic = NetProtocol::MAGIC_AI_ACTION;
            frame.seq = state.lastSeq + 1;
            frame.targetX = act.targetX;
            frame.targetRot = act.targetRot;
            frame.shouldHold = act.shouldHold ? 1 : 0;
            frame.shouldDrop = act.shouldDrop ? 1 : 0;
            frame.ready = act.ready ? 1 : 0;
            
            uint8_t buffer[NetProtocol::AIActionFrame::WIRE_SIZE];
            frame.toBytes(buffer);
            server.sendBinary(buffer, NetProtocol::AIActionFrame::WIRE_SIZE);
        }
        
        // 受信処理
        auto msg = server.recvBinary();
        if (msg) {
            if (msg->size() >= NetProtocol::InputEventFrame::WIRE_SIZE) {
                NetProtocol::InputEventFrame frame = 
                    NetProtocol::InputEventFrame::fromBytes(msg->data());
                if (frame.magic == NetProtocol::MAGIC_INPUT_EVENT) {
                    std::lock_guard<std::mutex> lock(state.queueMutex);
                    state.inputQueue.push(frame);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    std::cout << "[AI Server] Shutting down..." << std::endl;
    return 0;
}