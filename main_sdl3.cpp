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

struct OracleClient {
    NetWs::WsClient ws;
    bool connected = false;
    uint32_t nextBoardSeq = 1;
    uint32_t nextInputSeq = 1;
    uint32_t lastAppliedSeq = 0;

    bool connect(const std::string& host, uint16_t port) {
        connected = ws.connectTo(host, port);
        if (connected) ws.setNonBlocking(true);
        return connected;
    }

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

    bool pollAndApply(AIAction& act) {
        if (!connected) return false;
        bool applied = false;
        auto messages = ws.recvAllBinary();
        for (auto& msg : messages) {
            if (msg.size() < NetProtocol::AIActionFrame::WIRE_SIZE) continue;
            if (msg[0] != NetProtocol::MAGIC_AI_ACTION) continue;
            NetProtocol::AIActionFrame f = NetProtocol::AIActionFrame::fromBytes(msg.data());
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
                    printf("[templates] terrain_cache.dat を生成しました (次回起動時はこちらを使用)\n");
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

// ---- SDL Globals ----
SDL_Window* win = nullptr;
SDL_Renderer* ren = nullptr;
TTF_Font* font = nullptr;

// ---- 盤面テクスチャキャッシュ (静的レイヤー) ----
// 盤面(固定ブロック)は変化があったときだけこのテクスチャへ再描画し、
// 変化していないフレームはテクスチャをそのままコピーするだけにする。
// 落下中ピースなどの動的レイヤーはこのキャッシュの外で毎フレーム描く。
struct BoardRenderCache {
    SDL_Texture* texture = nullptr;
    BoardBits lastDrawnBoard{};
    bool hasDrawnOnce = false;

    void ensureTexture(SDL_Renderer* renderer, int texW, int texH) {
        if (texture) return;
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_TARGET, texW, texH);
        if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }

    bool isUpToDate(const BoardBits& current) const {
        if (!hasDrawnOnce) return false;
        return lastDrawnBoard == current;
    }

    void markDrawn(const BoardBits& current) {
        lastDrawnBoard = current;
        hasDrawnOnce = true;
    }

    void destroy() {
        if (texture) { SDL_DestroyTexture(texture); texture = nullptr; }
    }
};

BoardRenderCache p1BoardCache;
BoardRenderCache p2BoardCache;

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

    const int boardTexW = BOARD_W * CELL_SZ;
    const int boardTexH = BOARD_H * CELL_SZ;
    p1BoardCache.ensureTexture(ren, boardTexW, boardTexH);
    p2BoardCache.ensureTexture(ren, boardTexW, boardTexH);

    return true;
}

void drawRect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    SDL_FRect r{(float)x, (float)y, (float)w, (float)h};
    SDL_RenderFillRect(ren, &r);
}

// ---- 静的レイヤー: 盤面(固定ブロック)だけをテクスチャへ描く ----
// isUpToDateがfalseのとき(=盤面が前回描画時から変化しているとき)だけ呼ばれる。
void drawBoardToTexture(SDL_Renderer* renderer, SDL_Texture* target, const BoardBits& b) {
    SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, target);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            SDL_Color col = {40, 40, 40, 255};
            if (b[r] & (1 << c)) {
                col = {128, 128, 128, 255};
                for (int t = 0; t < 7; ++t) {
                    if (b[r] & (1 << c)) { col = COLORS[t]; break; }
                }
            }
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
            SDL_FRect rect{(float)(c * CELL_SZ), (float)(r * CELL_SZ),
                            (float)(CELL_SZ - 1), (float)(CELL_SZ - 1)};
            SDL_RenderFillRect(renderer, &rect);
        }
    }

    SDL_SetRenderTarget(renderer, prevTarget);
}

// ---- 盤面を描画する。前回描画時から変化がなければテクスチャをコピーするだけ。----
// ロジックが進んでいないフレームでは drawBoardToTexture は一切呼ばれない。
void drawBoardCached(BoardRenderCache& cache, const BoardBits& b, int ox, int oy) {
    if (!cache.isUpToDate(b)) {
        drawBoardToTexture(ren, cache.texture, b);
        cache.markDrawn(b);
    }
    SDL_FRect dst{(float)ox, (float)oy,
                   (float)(BOARD_W * CELL_SZ), (float)(BOARD_H * CELL_SZ)};
    SDL_RenderTexture(ren, cache.texture, nullptr, &dst);
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
    drawBoardCached(p1BoardCache, p1.board, 10, 40);
    if (!p1.gameOver) drawPiece(p1, 10, 40);
    
    drawText("P2 (AI)", 460, 10);
    drawBoardCached(p2BoardCache, p2.board, 460, 40);
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
    
    OracleClient oracleP2;
    OracleClient oracleP1;
    const std::string oracleHost = "127.0.0.1";
    const uint16_t oraclePort = 8080;
    bool oracleP2Connected = oracleP2.connect(oracleHost, oraclePort);
    if (!oracleP2Connected) {
        fprintf(stderr, "[Oracle] P2用サーバーへの接続に失敗しました(%s:%u)\n",
                oracleHost.c_str(), oraclePort);
    }
    const uint16_t oracleP1Port = 8081;
    bool oracleP1Connected = false;
    
    double softDropSpeed = 0.03;
    double aiDasDelay = 0.10, aiArrDelay = 0.02, aiThinkInterval = 0.10;
    bool paused = false, aiVsAi = false;
    bool quit = false;
    Uint64 last = SDL_GetTicks();
    Uint64 sessionStart = last;
    
    bool leftHeld = false, rightHeld = false;
    AIAction aiAct;
    AIAction p1Act;
    std::mt19937 garbageRng(2024);
    // セッション基準の絶対時計。P1/P2どちらの処理よりも前に、フレーム冒頭で
    // 一度だけ加算する。攻撃の着弾判定(fireTime+travelTimeとの比較)は
    // すべてこの時計を基準にするため、送り主側の処理速度に一切依存しない。
    double gameTimeNow = 0.0;
    // 攻撃発生から着弾までの標準所要時間(秒)。全消しの場合はfireAttack内部で0に上書きされる。
    const double kAttackTravelTime = 0.5;

    PType p2PrevCurType = p2.curType;
    PType p1PrevCurType = p1.curType;

    if (oracleP2Connected) oracleP2.sendBoardState(p2);

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
        gameTimeNow += dt;
        
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
            if (!paused && !aiVsAi && !p1.gameOver) {
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    switch (e.key.key) {
                        case SDLK_Z: p1.pendingSpawnRotDelta = 3; break;
                        case SDLK_X: p1.pendingSpawnRotDelta = 1; break;
                        default: break;
                    }
                } else if (e.type == SDL_EVENT_KEY_UP) {
                    switch (e.key.key) {
                        case SDLK_Z: case SDLK_X:
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
                        if (p1.pendingSpawnXDelta == -1) p1.pendingSpawnXDelta = rightHeld ? 1 : 0;
                    }
                    if (e.key.key == SDLK_RIGHT) {
                        rightHeld = false;
                        if (p1.pendingSpawnXDelta == 1) p1.pendingSpawnXDelta = leftHeld ? -1 : 0;
                    }
                    if (e.key.key == SDLK_DOWN) p1.softDrop = false;
                }
            }

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
            if (!aiVsAi && !p1.gameOver) {
                int dir = 0;
                if (leftHeld && !rightHeld) dir = -1;
                else if (rightHeld && !leftHeld) dir = 1;
                applyMoveRepeat(p1, dir, dt, 0.13, 0.02);
            }

            int p1ScoreBefore = p1.score;
            int p2ScoreBefore = p2.score;

            oracleP2.pollAndApply(aiAct);
            if (aiVsAi) oracleP1.pollAndApply(p1Act);

            if (aiVsAi && !p1.gameOver) {
                executeAI(p1, p1Act, dt, aiDasDelay, aiArrDelay);
            }

            if (!p2.gameOver) {
                executeAI(p2, aiAct, dt, aiDasDelay, aiArrDelay);
            }

            if (!p1.gameOver) stepPlayer(p1, dt, softDropSpeed);
            if (!p2.gameOver) stepPlayer(p2, dt, softDropSpeed);

            // curTypeの変化(=lockAndSpawnが起きたこと)は、PrevCurTypeを
            // 上書きする前にここでフラグとして控えておく。notifyMinoPlacedは
            // 「ミノが着地した瞬間」にだけ呼びたいので、この判定を後段でも使う。
            bool p1Locked = (p1.curType != p1PrevCurType);
            bool p2Locked = (p2.curType != p2PrevCurType);

            if (!aiVsAi && p2Locked) {
                oracleP2.sendBoardState(p2);
                aiAct.ready = false;
            }
            if (aiVsAi) {
                if (p2Locked) {
                    oracleP2.sendBoardState(p2);
                    aiAct.ready = false;
                }
                if (p1Locked) {
                    oracleP1.sendBoardState(p1);
                    p1Act.ready = false;
                }
            }
            p2PrevCurType = p2.curType;
            p1PrevCurType = p1.curType;

            auto checkSpawnCollision = [](PlayerState& ps) {
                if (ps.gameOver) return;
                const MinoShape& shape = SHAPES[(int)ps.curType][ps.curRot];
                if (IsCollision(ps.board, shape, ps.curX, ps.curY)) ps.gameOver = true;
            };
            checkSpawnCollision(p1);
            checkSpawnCollision(p2);

            // ---- ロックで発生したダメージを、相殺込みで一括確定する ----
            // fireAttackは「呼ばれた瞬間のincomingAttacks残高」とだけ相殺し、
            // 相殺しきれなかった分をディレイなしで即座に自分の盤面へ反映する。
            // 相殺後に残った自分の攻撃だけが、travelTime付きでoutgoingAttacksに積まれる。
            // 全消し(盤面が完全に空)の場合はtravelTime=0を渡し、即着弾にする。
            int p1Damage = (p1.score - p1ScoreBefore) / 10;
            if (p1Damage > 0) {
                bool p1PerfectClear = true;
                for (int r = 0; r < BOARD_H && p1PerfectClear; ++r)
                    if (p1.board[r] != 0) p1PerfectClear = false;
                fireAttack(p1, p1Damage, gameTimeNow, kAttackTravelTime, p1PerfectClear, garbageRng);
            }
            int p2Damage = (p2.score - p2ScoreBefore) / 10;
            if (p2Damage > 0) {
                bool p2PerfectClear = true;
                for (int r = 0; r < BOARD_H && p2PerfectClear; ++r)
                    if (p2.board[r] != 0) p2PerfectClear = false;
                fireAttack(p2, p2Damage, gameTimeNow, kAttackTravelTime, p2PerfectClear, garbageRng);
            }

            // ---- 相殺済みのoutgoingAttacksを、相手のincomingAttacksへ移すだけ ----
            // (相殺はfireAttackの中で既に完了しているので、ここではもう相殺しない)
            auto dispatchAttacks = [](PlayerState& sender, PlayerState& receiver) {
                for (auto& atk : sender.outgoingAttacks) {
                    receiver.incomingAttacks.push_back(atk);
                }
                sender.outgoingAttacks.clear();
            };
            dispatchAttacks(p1, p2);
            dispatchAttacks(p2, p1);

            // ---- 自分がミノを置いたら、自分に向かって来ている攻撃のnumMinosPlacedを進める ----
            if (p1Locked) notifyMinoPlaced(p1);
            if (p2Locked) notifyMinoPlaced(p2);

            // ---- 着弾判定 (絶対時刻比較。相手の処理速度には一切依存しない) ----
            advanceIncomingAttacks(p1, gameTimeNow, garbageRng);
            advanceIncomingAttacks(p2, gameTimeNow, garbageRng);
        }

        renderGame(p1, p2, softDropSpeed, aiDasDelay, aiArrDelay, aiThinkInterval, paused, aiVsAi);
    }

    p1BoardCache.destroy();
    p2BoardCache.destroy();
    if (font) TTF_CloseFont(font);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
