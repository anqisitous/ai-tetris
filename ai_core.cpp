// ===================================================================
// ai_core.cpp - Implementazione AI Decision Making
// ===================================================================
#include "ai_core.h"
#include <algorithm>
#include <cmath>

// ---- Pattern Memory (max 70) ----
void PatternMemory::addPattern(const PatternNeuron& p) {
    // Se già esiste simile, aumenta confidence
    for (auto& existing : patterns) {
        if (aspectDistance(existing.targetAspect, p.targetAspect) < 0.3f) {
            existing.confidence++;
            return;
        }
    }
    
    // Altrimenti aggiungi
    patterns.push_back(p);
    
    // Se supera 70, rimuovi il più simile ad altri
    if (patterns.size() > 70) {
        int worstIdx = 0;
        int maxSimilar = 0;
        for (size_t i = 0; i < patterns.size(); ++i) {
            int similar = 0;
            for (size_t j = 0; j < patterns.size(); ++j) {
                if (i != j && aspectDistance(patterns[i].targetAspect, 
                                              patterns[j].targetAspect) < 0.5f) {
                    similar++;
                }
            }
            if (similar > maxSimilar) {
                maxSimilar = similar;
                worstIdx = i;
            }
        }
        patterns.erase(patterns.begin() + worstIdx);
    }
}

PatternNeuron* PatternMemory::findBestMatch(const Aspect& current) {
    uint64_t hash = lshHash(current.values);
    
    PatternNeuron* best = nullptr;
    float bestDist = 1e9f;
    
    for (auto& p : patterns) {
        if ((p.lshHash ^ hash) < (1ULL << 10)) {
            float dist = aspectDistance(current, p.targetAspect);
            if (dist < bestDist && dist < 0.5f) {
                bestDist = dist;
                best = &p;
            }
        }
    }
    return best;
}

// ---- Funzione principale di decisione ----
AIAction decideAI(AIState& state, PlayerState& self, PlayerState& opp) {
    
    // 1. Estrai aspetto corrente del nemico
    Aspect currentAspect = extractAspect(opp.board, 
                                          self.linesCleared > 0 ? 1.0f : 0.0f,
                                          self.combo, self.next);
    
    // 2. Verifica template attivo
    if (state.activeTemplate != nullptr) {
        if (state.activeTemplate->matches(opp.board, self.next)) {
            // Template ancora valido: segui la sequenza
            auto target = state.activeTemplate->targetBoard();
            auto candidates = EnumerateAllPlacements(self.board, self.curType,
                                                      self.canHold, self.hold,
                                                      self.btb, self.combo);
            
            // Trova il piazzamento che avvicina al target
            PlacementResult* best = nullptr;
            int bestDist = 999;
            for (auto& c : candidates) {
                int dist = (GetTop3Rows(c.board) ^ target).count();
                if (dist < bestDist) {
                    bestDist = dist;
                    best = &c;
                }
            }
            
            if (best && bestDist < 10) {
                state.activeTemplate->advance();
                return makeAction(*best, best->usedHold);
            }
        } else {
            // Template non più valido
            state.activeTemplate = nullptr;
        }
    }
    
    // 3. Cerca nuovi template
    if (state.templateLib != nullptr && state.activeTemplate == nullptr) {
        auto matches = state.templateLib->match(opp.board, self.next);
        if (!matches.empty()) {
            // Usa il primo template matchato
            static ActiveTemplate active;
            active = matches[0];
            state.activeTemplate = &active;
            
            // Ricorsione per usare subito il template
            return decideAI(state, self, opp);
        }
    }
    
    // 4. パフェが狙えるならその手順を優先する
    if (state.enablePerfectClear) {
        AIAction pcAction = tryPerfectClearAction(state, self);
        if (pcAction.ready) return pcAction;
    }

    // 5. Nessun template: valutazione base
    auto candidates = EnumerateAllPlacements(self.board, self.curType,
                                              self.canHold, self.hold,
                                              self.btb, self.combo);
    
    if (candidates.empty()) {
        return AIAction{3, 0, false, true, true, false};
    }
    
    // 6. Valuta ogni candidato
    struct ScoredCandidate {
        PlacementResult result;
        float score;
    };
    std::vector<ScoredCandidate> scored;

    for (auto& c : candidates) {
        float score = evaluateCandidate(state, c, self.board, self.curType,
                                         self.next, self.hold, self.btb, self.combo);
        scored.push_back({c, score});
    }
    
    // 7. Ordina per punteggio
    std::sort(scored.begin(), scored.end(),
              [](const ScoredCandidate& a, const ScoredCandidate& b) {
                  return a.score > b.score;
              });
    
    // 8. Se il migliore è un nuovo pattern interessante, memorizzalo
    if (!scored.empty()) {
        Aspect bestAspect = extractAspect(scored[0].result.board, 0, 
                                           self.combo, self.next);
        if (!state.patternMemory.findBestMatch(bestAspect)) {
            PatternNeuron newPattern;
            newPattern.targetAspect = bestAspect;
            newPattern.depth = 1;
            newPattern.confidence = 1;
            newPattern.lshHash = lshHash(bestAspect.values);
            state.patternMemory.addPattern(newPattern);
        }
    }
    
    // 9. Restituisci l'azione migliore
    return makeAction(scored[0].result, scored[0].result.usedHold);
}

// ---- Valutazione di un singolo piazzamento ----
// decideAI と beamSearch の両方から使われる共通スコアリング関数。
// beforeBoard/curType は配置前の状態（T-Spin判定に必要）、
// next/hold/btb/combo はその時点でのプレイヤー状態を表す。
float evaluateCandidate(AIState& state, const PlacementResult& c,
                         const BoardBits& beforeBoard, PType curType,
                         const std::deque<PType>& next,
                         PType hold, int btb, int combo) {
    float score = 0.0f;

    // Check if we have I or T in hold
    bool hasHoldI = (hold == PType::I);
    bool hasHoldT = (hold == PType::T);

    // Danno inflitto
    score += c.damage * 15.0f;

    // Qualità del terreno risultante
    score += evaluateTerrainQuality(c.board);
    score += evaluateDoubleDaggerReadiness(c.board);

    // Spin evaluation (T-Spin + Tetris)
    SpinEvaluator spinEvaluator(btb > 0);
    SpinType spinType = spinEvaluator.getSpinType(beforeBoard, curType, c.x, c.y, c.rot);
    score += spinEvaluator.getScore(spinType, btb > 0);

    // Additional spin evaluation for the resulting board
    score += spinEvaluator.evaluate(c.board, next) * 0.5f;

    // New hole evaluation with reachable space analysis
    score += evaluateTerrainWithHoles(c.board, next, hasHoldI, hasHoldT);

    // Hole filling evaluation - can we fill holes with current/next/hold pieces?
    score += evaluateHoleFilling(c.board, next, hasHoldI, hasHoldT, curType);

    // 横パリティ: 奇数パリティ段が奇数個ならパフェ不能
    if (calculateHorizontalParity(c.board) % 2 == 1) {
        score -= EvalWeights::PARITY_PENALTY;
    }

    // Bonus memoria pattern
    Aspect newAspect = extractAspect(c.board, 0, combo, next);
    PatternNeuron* match = state.patternMemory.findBestMatch(newAspect);
    if (match && match->confidence > 2) {
        score += match->confidence * 5.0f;
    }

    // Penalità per hold (se non necessario)
    if (c.usedHold) score -= 2.0f;

    return score;
}

// ---- 実戦用の指し手決定 ----
AIAction decideAIMove(AIState& state, PlayerState& self) {
    if (state.enablePerfectClear) {
        AIAction pcAction = tryPerfectClearAction(state, self);
        if (pcAction.ready) return pcAction;
    }
    return makeActionFromBeam(beamSearch(state, self, 20, 3));
}

// ---- パフェ手順の一手目を AIAction にする ----
AIAction tryPerfectClearAction(AIState& state, const PlayerState& self) {
    AIAction act;

    std::deque<PType> queue = self.next;
    queue.push_front(self.curType);

    if (!hasPerfectClearChance(self.board, queue, self.hold, self.canHold,
                               state.pcOptions.maxRows)) {
        return act;
    }

    PCSearchResult pc = findPerfectClear(self.board, queue, self.hold, self.canHold,
                                          state.pcOptions);
    if (!pc.found || pc.moves.empty()) return act;

    const PCMove& first = pc.moves.front();
    act.targetX = first.x;
    act.targetRot = first.rot;
    act.shouldHold = first.usedHold;
    act.shouldDrop = true;
    act.ready = true;
    act.holdDone = false;
    act.hasPrediction = false;
    return act;
}

// ---- Beam Search ----
// 現在の盤面から searchDepth 手先まで探索し、各深さで上位 beamWidth 個の
// ノードだけを残しながら進める。最終的に最もスコアの高いノードの
// 「1手目の配置」を返す。
//
// 探索木のイメージ:
//   depth 0: [curType を置く] -> beamWidth 個の子ノードに絞る
//   depth 1: 各ノードの next[0] を置く -> 再び beamWidth 個に絞る
//   ...
//   depth N-1 まで繰り返し、最終スコアが最良のノードの firstMove を採用
//
// ノードごとに next キューを消費していくため、next が尽きたら
// そのノードはそれ以上展開しない（既存のスコアで留まる）。
BeamNode beamSearch(AIState& state, PlayerState& self,
                     int beamWidth, int searchDepth) {
    // ---- 深さ0: 現在のミノを置く ----
    auto rootCandidates = EnumerateAllPlacements(
        self.board, self.curType, self.canHold, self.hold, self.btb, self.combo);

    std::vector<BeamNode> beam;
    beam.reserve(rootCandidates.size());

    for (auto& c : rootCandidates) {
        float score = evaluateCandidate(state, c, self.board, self.curType,
                                         self.next, self.hold, self.btb, self.combo);

        BeamNode node;
        node.board = c.board;
        node.next = self.next;                 // 深さ1以降で先頭から消費する
        node.hold = c.usedHold ? self.curType : self.hold;
        node.canHold = true;                    // 1手ごとにホールドはリセットされる
        node.btb = c.tSpin || c.linesCleared == 4 ? self.btb + 1
                 : (c.linesCleared > 0 ? 0 : self.btb);
        node.combo = c.linesCleared > 0 ? self.combo + 1 : 0;
        node.score = score;
        node.hasFirstMove = true;
        node.firstX = c.x;
        node.firstY = c.y;
        node.firstRot = c.rot;
        node.firstUsedHold = c.usedHold;
        node.firstResult = c;

        beam.push_back(std::move(node));
    }

    if (beam.empty()) {
        return BeamNode{};  // 置ける場所がない（ゲームオーバー相当）
    }

    // 深さ0の時点でビーム幅に絞る
    auto trimToBeamWidth = [&](std::vector<BeamNode>& nodes) {
        if ((int)nodes.size() > beamWidth) {
            std::partial_sort(nodes.begin(), nodes.begin() + beamWidth, nodes.end(),
                               [](const BeamNode& a, const BeamNode& b) {
                                   return a.score > b.score;
                               });
            nodes.resize(beamWidth);
        } else {
            std::sort(nodes.begin(), nodes.end(),
                      [](const BeamNode& a, const BeamNode& b) {
                          return a.score > b.score;
                      });
        }
    };
    trimToBeamWidth(beam);

    // ---- 深さ1 以降: next キューを1つずつ消費しながら展開 ----
    for (int depth = 1; depth < searchDepth; ++depth) {
        std::vector<BeamNode> nextBeam;
        nextBeam.reserve(beam.size() * 4);

        for (auto& parent : beam) {
            if (parent.next.empty()) {
                // これ以上先読みできないノードはそのまま引き継ぐ
                nextBeam.push_back(parent);
                continue;
            }

            PType pieceType = parent.next.front();
            std::deque<PType> remainingNext(parent.next.begin() + 1, parent.next.end());

            auto candidates = EnumerateAllPlacements(
                parent.board, pieceType, parent.canHold, parent.hold,
                parent.btb, parent.combo);

            if (candidates.empty()) {
                // このノードはこれ以上展開できない（詰み）ので低評価のまま残す
                nextBeam.push_back(parent);
                continue;
            }

            for (auto& c : candidates) {
                float addScore = evaluateCandidate(state, c, parent.board, pieceType,
                                                    remainingNext, parent.hold,
                                                    parent.btb, parent.combo);

                BeamNode child = parent;  // firstMove などルート情報を引き継ぐ
                child.board = c.board;
                child.next = remainingNext;
                child.hold = c.usedHold ? pieceType : parent.hold;
                child.canHold = true;
                child.btb = c.tSpin || c.linesCleared == 4 ? parent.btb + 1
                          : (c.linesCleared > 0 ? 0 : parent.btb);
                child.combo = c.linesCleared > 0 ? parent.combo + 1 : 0;
                child.score = parent.score + addScore;  // 累積スコア

                nextBeam.push_back(std::move(child));
            }
        }

        if (nextBeam.empty()) break;  // 全ノードが展開不能なら打ち切り
        trimToBeamWidth(nextBeam);
        beam = std::move(nextBeam);
    }

    // ---- 最終的に最良のノードを返す ----
    return *std::max_element(beam.begin(), beam.end(),
                              [](const BeamNode& a, const BeamNode& b) {
                                  return a.score < b.score;
                              });
}

// ---- Beam Search の結果から AIAction を組み立てる ----
AIAction makeActionFromBeam(const BeamNode& node) {
    if (!node.hasFirstMove) {
        // 置ける場所がなかった場合のフォールバック
        return AIAction{3, 0, false, true, true, false};
    }
    return makeAction(node.firstResult, node.firstUsedHold);
}

// ---- Crea azione dal piazzamento ----
AIAction makeAction(const PlacementResult& best, bool usedHold) {
    AIAction act;
    act.targetX = best.x;
    act.targetRot = best.rot;
    act.shouldHold = usedHold;
    act.shouldDrop = true;
    act.ready = true;
    act.holdDone = false;
    
    // Predizione (semplificata)
    act.predictedDamage = best.damage;
    act.predictedOppBoard = extractAspect(best.board);
    act.beforeBoard = extractAspect(best.board);
    act.hasPrediction = true;
    
    return act;
}

// ---- 単純な非キック回転を1ステップ試みる ----
// AI操作専用の簡易回転。SRSキックは使わず、その場での回転可否のみ判定する。
// 塞がっている場合はfalseを返し、呼び出し側は横移動を先に進める。
static bool tryRotateStep(PlayerState& ps, int dir) {
    int newRot = (ps.curRot + dir + 4) % 4;
    const MinoShape& shape = SHAPES[(int)ps.curType][newRot];
    if (!IsCollision(ps.board, shape, ps.curX, ps.curY)) {
        ps.curRot = newRot;
        return true;
    }
    return false;
}

// ---- Esecuzione azione con DAS/ARR ----
// AIActionが持つtargetX/targetRotに向けて、hold -> rotate -> move -> drop の
// 順で毎フレーム少しずつ入力を進める。EnumerateAllPlacementsのBFSが返す
// 座標は到達可能性を保証済みなので、この単純な逐次操作でも最終的に到達する。
void executeAI(PlayerState& ps, AIAction& act, double dt, double das, double arr) {
    if (!act.ready) {
        ps.moveTimer = 0;
        ps.moveDir = 0;
        return;
    }

    // Hold
    if (act.shouldHold && !act.holdDone) {
        holdPiece(ps);
        act.holdDone = true;
        return;
    }

    // Rotazione (回転優先だが詰まっていれば横移動へフォールバック)
    if (ps.curRot != act.targetRot) {
        if (tryRotateStep(ps, 1)) return;
        // 回転が阻害されている場合は横移動を先に進めて隙間を作る
        if (ps.curX != act.targetX) {
            int dir = (ps.curX < act.targetX) ? 1 : -1;
            applyMoveRepeat(ps, dir, dt, das, arr);
        }
        return;
    }

    // Movimento laterale
    if (ps.curX != act.targetX) {
        int dir = (ps.curX < act.targetX) ? 1 : -1;
        applyMoveRepeat(ps, dir, dt, das, arr);
        return;
    }

    // Hard drop
    ps.moveTimer = 0;
    ps.moveDir = 0;
    if (act.shouldDrop) {
        hardDropPlayer(ps);
    }
    act.ready = false;
    act.holdDone = false;
}

// ---- Apprendimento ----
void learnFromPlacement(AIState& state, const Aspect& before,
                         const Aspect& after, float reward) {
    PatternNeuron pn;
    pn.targetAspect = after;
    pn.depth = 1;
    pn.confidence = 1;
    pn.lshHash = lshHash(after.values);
    state.patternMemory.addPattern(pn);
}
