// ===================================================================
// main_sdl3.cpp - Gioco completo con AI
// ===================================================================
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "game_engine.h"
#include "ai_core.h"
#include <cstdio>
#include <cmath>
#include <random>

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
    
    // aiState/aiAct は P2 用。P1がAIVsAiモードに切り替わったときは p1AiState/p1Act を使う。
    AIState aiState;
    AIState p1AiState;
    TemplateLibrary templateLib;
    aiState.templateLib = &templateLib;
    aiState.patternMemory = PatternMemory();
    p1AiState.templateLib = &templateLib;
    p1AiState.patternMemory = PatternMemory();
    
    double softDropSpeed = 0.03;
    double aiDasDelay = 0.10, aiArrDelay = 0.02, aiThinkInterval = 0.10;
    bool paused = false, aiVsAi = false;
    bool quit = false;
    Uint64 last = SDL_GetTicks();
    
    bool leftHeld = false, rightHeld = false;
    AIAction aiAct;    // P2の行動
    AIAction p1Act;    // P1がAI操作のときの行動
    std::mt19937 garbageRng(2024);
    
    while (!quit) {
        Uint64 now = SDL_GetTicks();
        double dt = (now - last) / 1000.0;
        last = now;
        
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit = true;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                switch (e.key.key) {
                    case SDLK_T: aiVsAi = !aiVsAi; leftHeld = rightHeld = false; p1.softDrop = false; break;
                    case SDLK_P: paused = !paused; break;
                    case SDLK_R:
                        p1.init(123); p2.init(456);
                        aiState = AIState(); aiState.templateLib = &templateLib;
                        p1AiState = AIState(); p1AiState.templateLib = &templateLib;
                        aiAct = AIAction{}; p1Act = AIAction{};
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
                        case SDLK_Z: { const MinoShape& s = SHAPES[(int)p1.curType][(p1.curRot + 3) % 4];
                            if (!IsCollision(p1.board, s, p1.curX, p1.curY)) p1.curRot = (p1.curRot + 3) % 4; } break;
                        case SDLK_X: { const MinoShape& s = SHAPES[(int)p1.curType][(p1.curRot + 1) % 4];
                            if (!IsCollision(p1.board, s, p1.curX, p1.curY)) p1.curRot = (p1.curRot + 1) % 4; } break;
                        case SDLK_C: holdPiece(p1); break;
                        case SDLK_UP: hardDropPlayer(p1); break;
                    }
                }
            }
            if (!aiVsAi) {
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    if (e.key.key == SDLK_LEFT) leftHeld = true;
                    if (e.key.key == SDLK_RIGHT) rightHeld = true;
                    if (e.key.key == SDLK_DOWN) p1.softDrop = true;
                } else if (e.type == SDL_EVENT_KEY_UP) {
                    if (e.key.key == SDLK_LEFT) leftHeld = false;
                    if (e.key.key == SDLK_RIGHT) rightHeld = false;
                    if (e.key.key == SDLK_DOWN) p1.softDrop = false;
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

            if (aiVsAi && !p1.gameOver) {
                p1AiState.thinkTimer += dt;
                if (!p1Act.ready && p1AiState.thinkTimer >= aiThinkInterval) {
                    p1AiState.thinkTimer = 0;
                    BeamNode node = beamSearch(p1AiState, p1, 20, 3);
                    p1Act = makeActionFromBeam(node);
                }
                executeAI(p1, p1Act, dt, aiDasDelay, aiArrDelay);
            }

            if (!p2.gameOver) {
                aiState.thinkTimer += dt;
                if (!aiAct.ready && aiState.thinkTimer >= aiThinkInterval) {
                    aiState.thinkTimer = 0;
                    BeamNode node = beamSearch(aiState, p2, 20, 3);
                    aiAct = makeActionFromBeam(node);
                }
                executeAI(p2, aiAct, dt, aiDasDelay, aiArrDelay);
            }

            // 重力落下（人間操作時のP1にも、AI操作中の両者にも共通して働く）
            if (!p1.gameOver) stepPlayer(p1, dt, softDropSpeed);
            if (!p2.gameOver) stepPlayer(p2, dt, softDropSpeed);

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