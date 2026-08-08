// ===================================================================
// main_sdl3.cpp - Gioco completo con AI
// ===================================================================
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "game_engine.h"
#include "ai_core.h"
#include "ws_client.h"
#include "protocol.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <fstream>
#include <optional>

// ===================================================================
// Oracleクライアント: AI側(P2、およびaiVsAi時のP1)の一手判断を
// oracleサーバーに問い合わせるための薄いラッパー。
//
// 設計方針(protocol.hのコメント参照):
//   - 盤面はAIが一手打つ(着地する)たびにBoardStateFrameとして送る。
//   - InputEventFrameは人間側P1の生キー操作を非同期ストリームで送る。
//     (現状oracle側は判断に使わないが、将来のneuron拡張に備えて送る)
//   - AIActionFrameの到着は待たない。executeAI側でready判定により
//     AI側ピースの移動確定だけを保留する(通常モードのみ。
//     aiVsAiモードでは保留せず、直近の指示のまま進める)。
// ===================================================================
struct OracleClient {
    NetWs::WsClient ws;
    bool connected = false;
    uint32_t nextBoardSeq = 1;
    uint32_t nextInputSeq = 1;
    uint32_t lastAppliedSeq = 0;  // 直近でaiActへ反映したAIActionFrameのseq

    bool connect(const std::string& host, uint16_t port) {
        connected = ws.connectTo(host, port);
        if (connected) ws.setNonBlocking(true);
        return connected;
    }

    // 着地時に呼ぶ。盤面全体を送信し、新しいseqを払い出して返す。
    uint32_t sendBoardState(const PlayerState& ps) {
        if (!connected) return 0;
        NetProtocol::BoardStateFrame f;
        f.seq = nextBoardSeq++;
        for (size_t i = 0; i < NetProtocol::BoardStateFrame::BOARD_ROWS; ++i) {
            f.boardRows[i] = ps.board[i];
        }
        f.curType = static_cast<uint8_t>(ps.curType);
        f.curRot = static_cast<uint8_t>(ps.curRot);
        f.curX = ps.curX;
        f.curY = ps.curY;
        for (size_t i = 0; i < NetProtocol::BoardStateFrame::NEXT_COUNT; ++i) {
            f.nextTypes[i] = (i < ps.next.size())
                ? static_cast<uint8_t>(ps.next[i])
                : static_cast<uint8_t>(PType::I);
        }
        f.holdType = static_cast<uint8_t>(ps.hold);
        f.canHold = ps.canHold ? 1 : 0;
        f.combo = ps.combo;
        f.btb = ps.btb;

        uint8_t buffer[NetProtocol::BoardStateFrame::WIRE_SIZE];
        f.toBytes(buffer);
        ws.sendBinary(buffer, NetProtocol::BoardStateFrame::WIRE_SIZE);
        return f.seq;
    }

    // 人間側P1の生キー操作を非同期で送る(判断には使わないが常時送り続ける)。
    void sendInputEvent(NetProtocol::KeyCode key, uint32_t timestampMs) {
        if (!connected) return;
        NetProtocol::InputEventFrame f;
        f.seq = nextInputSeq++;
        f.keyCode = static_cast<uint8_t>(key);
        f.timestampMs = timestampMs;
        uint8_t buffer[NetProtocol::InputEventFrame::WIRE_SIZE];
        f.toBytes(buffer);
        ws.sendBinary(buffer, NetProtocol::InputEventFrame::WIRE_SIZE);
    }

    // 毎フレーム呼ぶ。届いているAIActionFrameのうち最新のものだけをaiActへ反映する。
    // 戻り値: 何か新しい応答を反映したらtrue。
    bool pollAndApply(AIAction& act) {
        if (!connected) return false;
        bool applied = false;
        auto messages = ws.recvAllBinary();
        for (auto& msg : messages) {
            if (msg.size() < NetProtocol::AIActionFrame::WIRE_SIZE) continue;
            if (msg[0] != NetProtocol::MAGIC_AI_ACTION) continue;
            NetProtocol::AIActionFrame f = NetProtocol::AIActionFrame::fromBytes(msg.data());
            // 古い(seqが遅れて届いた)応答は無視する。
            if (f.seq <= lastAppliedSeq && lastAppliedSeq != 0) continue;
            lastAppliedSeq = f.seq;

            act.targetX = f.targetX;
            act.targetRot = f.targetRot;
            act.shouldHold = (f.shouldHold != 0);
            act.shouldDrop = (f.shouldDrop != 0);
            act.ready = (f.ready != 0);
            act.holdDone = false;
            applied = true;
        }
        return applied;
    }
};

// ---- 地形テンプレートの読み込み ----
// 起動時に以下の優先順で地形を組み込む:
//   1. terrain_cache.dat (バイナリキャッシュ) があればそれを最優先で高速読込
//   2. fumen_list.txt (地形ID + fumen文字列のリスト) があれば読み込み、
//      同時にバイナリキャッシュを生成して次回以降に備える
//   3. templates.tmpl (手書き地形 + fumen埋め込み両対応の定義ファイル)
// これらは同じTemplateLibraryに統合され、机械的に(探索なしで)先頭から順に
// マッチングが試みられる。
void loadAllTemplates(TemplateLibrary& lib) {
    const char* cachePath = "terrain_cache.dat";
    const char* fumenListPath = "fumen_list.txt";
    const char* tmplPath = "templates.tmpl";

    int loadedFromCache = TemplateLoader::loadTerrainCache(cachePath, lib);
    if (loadedFromCache > 0) {
        printf("[templates] terrain_cache.dat から %d 件の地形を読み込みました\n", loadedFromCache);
    } else {
        // キャッシュが無い/空の場合はfumenリストから読み込み、キャッシュを新規作成する
        std::ifstream check(fumenListPath);
        if (check.is_open()) {
            check.close();
            int loadedFromFumen = TemplateLoader::loadFumenListFile(fumenListPath, lib, 3);
            printf("[templates] fumen_list.txt から %d 件の地形を読み込みました\n", loadedFromFumen);
            if (loadedFromFumen > 0) {
                bool built = TemplateLoader::buildTerrainCache(fumenListPath, cachePath, 3);
                if (built) {
                    printf("[templates] terrain_cache.dat を生成しました (次回起動時はこちらを使用)\n");
                }
            }
        }
    }

    // 手書きテンプレート(fumen埋め込み含む)は常にtmplファイルから追加で読み込む
    std::ifstream tmplCheck(tmplPath);
    if (tmplCheck.is_open()) {
        tmplCheck.close();
        int loadedFromTmpl = TemplateLoader::loadTmplFile(tmplPath, lib);
        printf("[templates] templates.tmpl から %d 件の地形を読み込みました\n", loadedFromTmpl);
    }
}

// ---- SDL Globals ----
SDL_Window* win = nullptr;
SDL_Renderer* ren = nullptr;
TTF_Font* font = nullptr;

bool initSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    if (!TTF_Init()) return false;
    win = SDL_CreateWindow("Tetris AI", WIN_W, WIN_H, 0);
    if (!win) return false;
    ren = SDL_CreateRenderer(win, nullptr);
    if (!ren) return false;
    const char* fonts[] = {"arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", nullptr};
    for (int i = 0; fonts[i]; ++i)
        if ((font = TTF_OpenFont(fonts[i], 18))) break;
    return true;
}

void drawRect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    SDL_FRect r{(float)x, (float)y, (float)w, (float)h};
    SDL_RenderFillRect(ren, &r);
}

void drawBoard(const BoardBits& b, int ox, int oy) {
    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            SDL_Color col = {40,40,40,255};
            if (b[r] & (1 << c)) {
                col = {128,128,128,255};
                for (int t = 0; t < 7; ++t) {
                    if (b[r] & (1 << c)) { col = COLORS[t]; break; }
                }
            }
            drawRect(ox + c * CELL_SZ, oy + r * CELL_SZ, CELL_SZ - 1, CELL_SZ - 1, col);
        }
    }
}

void drawPiece(const PlayerState& ps, int ox, int oy) {
    const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
    SDL_Color col = COLORS[(int)ps.curType];
    for (int r = 0; r < shape.height; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            uint16_t mask = shape.rows[r];
            if (ps.curX >= 0) mask <<= ps.curX;
            else mask >>= (-ps.curX);
            if (mask & (1 << c)) {
                int drawY = ps.curY + r;
                if (drawY >= 0 && drawY < BOARD_H)
                    drawRect(ox + c * CELL_SZ, oy + drawY * CELL_SZ, CELL_SZ - 1, CELL_SZ - 1, col);
            }
        }
    }
}

void drawText(const std::string& txt, int x, int y) {
    if (!font) return;
    SDL_Surface* s = TTF_RenderText_Solid(font, txt.c_str(), 0, {255,255,255,255});
    if (!s) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, s);
    SDL_FRect dst{(float)x, (float)y, (float)s->w, (float)s->h};
    SDL_RenderTexture(ren, tex, nullptr, &dst);
    SDL_DestroySurface(s);
    SDL_DestroyTexture(tex);
}

void renderGame(PlayerState& p1, PlayerState& p2, double softSpeed,
                double das, double arr, double think, bool paused, bool aiVsAi) {
    SDL_SetRenderDrawColor(ren, 20, 20, 60, 255);
    SDL_RenderClear(ren);
    
    drawText("P1", 10, 10);
    drawBoard(p1.board, 10, 40);
    if (!p1.gameOver) drawPiece(p1, 10, 40);
    
    drawText("P2 (AI)", 460, 10);
    drawBoard(p2.board, 460, 40);
    if (!p2.gameOver) drawPiece(p2, 460, 40);
    
    int ty = 650;
    drawText("Mode: " + std::string(aiVsAi ? "AI vs AI" : "Human vs AI") + " (T)", 10, ty);
    ty += 22;
    drawText("SoftDrop: " + std::to_string(softSpeed) + " (+/-)", 10, ty);
    ty += 22;
    char buf[128];
    snprintf(buf, sizeof(buf), "AI: DAS=%.3f ARR=%.3f Think=%.3f ([ ])", das, arr, think);
    drawText(buf, 10, ty);
    ty += 22;
    drawText("P1: Arrows=move Z/X=rot C=hold", 10, ty);
    if (paused) drawText("PAUSED (P)", 10, ty + 22);
    
    SDL_RenderPresent(ren);
}

int main() {
    if (!initSDL()) return 1;
    
    PlayerState p1, p2;
    p1.init(123);
    p2.init(456);
    
    // AI(P2、およびaiVsAi時のP1)の一手判断はoracleサーバーに問い合わせる。
    // ローカルではbeamSearchを直接呼ばない。P1用・P2用で別接続を持つ
    // (BoardStateFrame.seqの空間を分け、応答の取り違えを避けるため)。
    OracleClient oracleP2;
    OracleClient oracleP1;
    const std::string oracleHost = "127.0.0.1";
    const uint16_t oraclePort = 8080;
    bool oracleP2Connected = oracleP2.connect(oracleHost, oraclePort);
    if (!oracleP2Connected) {
        fprintf(stderr, "[Oracle] P2用サーバーへの接続に失敗しました(%s:%u)\n",
                oracleHost.c_str(), oraclePort);
    }
    // P1用は aiVsAi モードに切り替えたときに初めて必要になるため遅延接続でもよいが、
    // 単一サーバーは1接続限定(ws_server.hの制約)なので、P1側を使う場合は
    // 別ポートの2つ目のoracleサーバーインスタンスが必要になる。
    // ここでは同一ホストの別ポート(8081)を想定する。
    const uint16_t oracleP1Port = 8081;
    bool oracleP1Connected = false;  // aiVsAiへの切り替え時に遅延接続する
    
    double softDropSpeed = 0.03;
    double aiDasDelay = 0.10, aiArrDelay = 0.02, aiThinkInterval = 0.10;
    bool paused = false, aiVsAi = false;
    bool quit = false;
    Uint64 last = SDL_GetTicks();
    Uint64 sessionStart = last;
    
    bool leftHeld = false, rightHeld = false;
    AIAction aiAct;    // P2の行動
    AIAction p1Act;    // P1がAI操作のときの行動
    std::mt19937 garbageRng(2024);

    // 着地検出用: 直前フレームでのcurTypeを保持し、変化したら着地とみなす。
    // (lockAndSpawnは必ずps.curType = ps.popNext()を行うため、curTypeの変化は
    //  着地が発生したことの確実な検出条件になる。ライン消去の有無やscoreの
    //  変化に依存しないため、無得点の着地も取りこぼさない。)
    PType p2PrevCurType = p2.curType;
    PType p1PrevCurType = p1.curType;

    // ゲーム開始時点のP2盤面(最初のミノ)をoracleへ一度送っておく。
    // 着地イベント(curTypeの変化)は次のミノに切り替わった時にしか発火しないため、
    // これを送らないと最初のミノについてoracleが何も知らないまま放置される。
    if (oracleP2Connected) oracleP2.sendBoardState(p2);

    // SDLキー -> NetProtocol::KeyCode 変換。対応しないキーはnulloptを返す。
    auto toKeyCode = [](SDL_Keycode key, bool isDown) -> std::optional<NetProtocol::KeyCode> {
        switch (key) {
            case SDLK_LEFT:  return isDown ? NetProtocol::KeyCode::LeftDown  : NetProtocol::KeyCode::LeftUp;
            case SDLK_RIGHT: return isDown ? NetProtocol::KeyCode::RightDown : NetProtocol::KeyCode::RightUp;
            case SDLK_X:     return isDown ? std::optional(NetProtocol::KeyCode::RotateCW)  : std::nullopt;
            case SDLK_Z:     return isDown ? std::optional(NetProtocol::KeyCode::RotateCCW) : std::nullopt;
            case SDLK_UP:    return isDown ? std::optional(NetProtocol::KeyCode::HardDrop)   : std::nullopt;
            case SDLK_C:     return isDown ? std::optional(NetProtocol::KeyCode::Hold)       : std::nullopt;
            case SDLK_DOWN:  return isDown ? NetProtocol::KeyCode::SoftDropOn : NetProtocol::KeyCode::SoftDropOff;
            default: return std::nullopt;
        }
    };
    
    while (!quit) {
        Uint64 now = SDL_GetTicks();
        double dt = (now - last) / 1000.0;
        last = now;
        
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit = true;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                switch (e.key.key) {
                    case SDLK_T:
                        aiVsAi = !aiVsAi; leftHeld = rightHeld = false; p1.softDrop = false;
                        p1.pendingSpawnRotDelta = 0; p1.pendingSpawnXDelta = 0;
                        if (aiVsAi) {
                            if (!oracleP1Connected) {
                                oracleP1Connected = oracleP1.connect(oracleHost, oracleP1Port);
                                if (!oracleP1Connected) {
                                    fprintf(stderr, "[Oracle] P1用サーバーへの接続に失敗しました(%s:%u)\n",
                                            oracleHost.c_str(), oracleP1Port);
                                }
                            }
                            // 切り替え直後の現在のミノをoracleへ伝えておく
                            // (次の着地までoracleが古いか空の情報しか持たないのを防ぐ)
                            if (oracleP1Connected) {
                                oracleP1.sendBoardState(p1);
                                p1Act.ready = false;
                            }
                        }
                        break;
                    case SDLK_P: paused = !paused; break;
                    case SDLK_R:
                        p1.init(123); p2.init(456);
                        aiAct = AIAction{}; p1Act = AIAction{};
                        p2PrevCurType = p2.curType;
                        p1PrevCurType = p1.curType;
                        if (oracleP2Connected) oracleP2.sendBoardState(p2);
                        if (aiVsAi && oracleP1Connected) oracleP1.sendBoardState(p1);
                        break;
                    case SDLK_ESCAPE: quit = true; break;
                    case SDLK_LEFTBRACKET:
                        aiDasDelay = std::min(0.30, aiDasDelay + 0.01);
                        aiArrDelay = std::min(0.10, aiArrDelay + 0.002);
                        aiThinkInterval = std::min(0.30, aiThinkInterval + 0.01);
                        break;
                    case SDLK_RIGHTBRACKET:
                        aiDasDelay = std::max(0.01, aiDasDelay - 0.01);
                        aiArrDelay = std::max(0.005, aiArrDelay - 0.002);
                        aiThinkInterval = std::max(0.02, aiThinkInterval - 0.01);
                        break;
                    case SDLK_PLUS: case SDLK_EQUALS:
                        if (paused || p1.gameOver || p2.gameOver) softDropSpeed = std::max(0.01, softDropSpeed - 0.01);
                        break;
                    case SDLK_MINUS:
                        if (paused || p1.gameOver || p2.gameOver) softDropSpeed = std::min(0.20, softDropSpeed + 0.01);
                        break;
                }
                if (!paused && !aiVsAi && !p1.gameOver) {
                    switch (e.key.key) {
                        case SDLK_Z: {
                            // 現在のミノへその場で反映(従来通り。キーリピートでは反映しない)
                            if (!e.key.repeat) {
                                const MinoShape& s = SHAPES[(int)p1.curType][(p1.curRot + 3) % 4];
                                if (!IsCollision(p1.board, s, p1.curX, p1.curY)) p1.curRot = (p1.curRot + 3) % 4;
                            }
                        } break;
                        case SDLK_X: {
                            if (!e.key.repeat) {
                                const MinoShape& s = SHAPES[(int)p1.curType][(p1.curRot + 1) % 4];
                                if (!IsCollision(p1.board, s, p1.curX, p1.curY)) p1.curRot = (p1.curRot + 1) % 4;
                            }
                        } break;
                        case SDLK_C: if (!e.key.repeat) holdPiece(p1); break;
                        case SDLK_UP: if (!e.key.repeat) hardDropPlayer(p1); break;
                    }
                }
            }
            // ---- 先行入力バッファの更新 ----
            // 「押している間は記録し続け、離したら記録を消す」方式。
            // キーリピートイベントも含めて処理し、長押し中は常に最新の状態を保持する。
            if (!paused && !aiVsAi && !p1.gameOver) {
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    switch (e.key.key) {
                        case SDLK_Z: p1.pendingSpawnRotDelta = 3; break; // 反時計回り。押している間は保持
                        case SDLK_X: p1.pendingSpawnRotDelta = 1; break; // 時計回り
                        default: break;
                    }
                } else if (e.type == SDL_EVENT_KEY_UP) {
                    switch (e.key.key) {
                        case SDLK_Z: case SDLK_X:
                            // 離したキーが「現在保持中の先行入力」と同じ回転方向のときだけクリアする。
                            // (Z押しっぱなし→Xも押す→Zだけ離す、のような場合にXの意図を消さないため)
                            if ((e.key.key == SDLK_Z && p1.pendingSpawnRotDelta == 3) ||
                                (e.key.key == SDLK_X && p1.pendingSpawnRotDelta == 1)) {
                                p1.pendingSpawnRotDelta = 0;
                            }
                            break;
                        default: break;
                    }
                }
            }
            if (!aiVsAi) {
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    if (e.key.key == SDLK_LEFT) { leftHeld = true; p1.pendingSpawnXDelta = -1; }
                    if (e.key.key == SDLK_RIGHT) { rightHeld = true; p1.pendingSpawnXDelta = 1; }
                    if (e.key.key == SDLK_DOWN) p1.softDrop = true;
                } else if (e.type == SDL_EVENT_KEY_UP) {
                    if (e.key.key == SDLK_LEFT) {
                        leftHeld = false;
                        // 離したのが「現在保持中の先行入力」と同じ方向のときだけクリアする
                        if (p1.pendingSpawnXDelta == -1) p1.pendingSpawnXDelta = rightHeld ? 1 : 0;
                    }
                    if (e.key.key == SDLK_RIGHT) {
                        rightHeld = false;
                        if (p1.pendingSpawnXDelta == 1) p1.pendingSpawnXDelta = leftHeld ? -1 : 0;
                    }
                    if (e.key.key == SDLK_DOWN) p1.softDrop = false;
                }
            }

            // ---- 人間側P1の生キー操作をoracleへ非同期ストリームで送る ----
            // (aiVsAi中はP1もAIが操作するため人間の入力ではなくなり、送らない)
            if (!aiVsAi && (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
                && !e.key.repeat) {
                auto kc = toKeyCode(e.key.key, e.type == SDL_EVENT_KEY_DOWN);
                if (kc.has_value()) {
                    uint32_t ts = static_cast<uint32_t>(SDL_GetTicks() - sessionStart);
                    oracleP2.sendInputEvent(*kc, ts);
                }
            }
        }
        
        if (!paused) {
            // P1 (Human or AI)
            if (!aiVsAi && !p1.gameOver) {
                int dir = 0;
                if (leftHeld && !rightHeld) dir = -1;
                else if (rightHeld && !leftHeld) dir = 1;
                applyMoveRepeat(p1, dir, dt, 0.13, 0.02);
            }

            // score差分でロック時のダメージ発生を検知するため、ステップ前の値を控える
            int p1ScoreBefore = p1.score;
            int p2ScoreBefore = p2.score;

            // ---- oracleからの最新応答をaiActへ反映 ----
            // (応答を待たずに毎フレーム進む。届いていればreadyの値ごと反映するだけ)
            oracleP2.pollAndApply(aiAct);
            if (aiVsAi) oracleP1.pollAndApply(p1Act);

            if (aiVsAi && !p1.gameOver) {
                // AI vs AiモードではP1側もoracleが判断するが、readyを待たずに
                // 直近の指示のままexecuteAIを進める(未着のときはact.ready=falseの
                // ままなのでexecuteAIは何もせず、次に応答が届いた時点で動き出す。
                // これは「待つ」のではなく、単に「まだ指示がない」状態と同じ扱い)。
                executeAI(p1, p1Act, dt, aiDasDelay, aiArrDelay);
            }

            if (!p2.gameOver) {
                // 通常モード(Human vs AI)・AI vs AiモードともP2は常にoracle判断。
                // 通常モードではready==falseの間、executeAIが何もしないことで
                // 「AI側ピースの移動確定だけを保留する」待ち合わせが実現される。
                executeAI(p2, aiAct, dt, aiDasDelay, aiArrDelay);
            }

            // 重力落下（人間操作時のP1にも、AI操作中の両者にも共通して働く）
            if (!p1.gameOver) stepPlayer(p1, dt, softDropSpeed);
            if (!p2.gameOver) stepPlayer(p2, dt, softDropSpeed);

            // ---- 着地検出とBoardStateFrame送信 ----
            // lockAndSpawnは必ずcurTypeを更新するため、前フレームからの変化を
            // 「一手打たれた(着地した)」の確実な検出条件として使う。
            // 検出したらoracleへ新しい盤面を送り、ready状態をリセットする
            // (次の着地までの間、古いreadyのまま誤って動き続けないようにする)。
            if (!aiVsAi && p2.curType != p2PrevCurType) {
                oracleP2.sendBoardState(p2);
                aiAct.ready = false;
            }
            if (aiVsAi) {
                if (p2.curType != p2PrevCurType) {
                    oracleP2.sendBoardState(p2);
                    aiAct.ready = false;
                }
                if (p1.curType != p1PrevCurType) {
                    oracleP1.sendBoardState(p1);
                    p1Act.ready = false;
                }
            }
            p2PrevCurType = p2.curType;
            p1PrevCurType = p1.curType;

            // スポーン位置での衝突をゲームオーバーとして扱う
            // game_engine.cpp 側にこの判定が無いため、ここで補完する
            auto checkSpawnCollision = [](PlayerState& ps) {
                if (ps.gameOver) return;
                const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
                if (IsCollision(ps.board, shape, ps.curX, ps.curY)) ps.gameOver = true;
            };
            checkSpawnCollision(p1);
            checkSpawnCollision(p2);

            // ロックで発生したダメージは、送り主自身のoutgoingAttacksに積む
            // （outgoingAttacks = そのプレイヤーが繰り出した攻撃、という向きで統一）
            int p1Damage = p1.score - p1ScoreBefore;
            int p2Damage = p2.score - p2ScoreBefore;
            if (p1Damage > 0) p1.outgoingAttacks.push_back({0.0, p1Damage / 10});
            if (p2Damage > 0) p2.outgoingAttacks.push_back({0.0, p2Damage / 10});

            // 送り主側の保留分を取り出し、対戦相手の盤面へガベージとして反映する
            auto dispatchAttacks = [&](PlayerState& sender, PlayerState& receiver) {
                int totalLines = 0;
                for (auto& atk : sender.outgoingAttacks) totalLines += atk.damage;
                sender.outgoingAttacks.clear();
                if (totalLines > 0 && !receiver.gameOver) AddGarbage(receiver.board, totalLines, garbageRng);
            };
            dispatchAttacks(p1, p2);
            dispatchAttacks(p2, p1);
        }

        renderGame(p1, p2, softDropSpeed, aiDasDelay, aiArrDelay, aiThinkInterval, paused, aiVsAi);
    }

    if (font) TTF_CloseFont(font);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}