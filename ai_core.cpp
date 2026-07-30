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
    
    // 4. Nessun template: valutazione base
    auto candidates = EnumerateAllPlacements(self.board, self.curType,
                                              self.canHold, self.hold,
                                              self.btb, self.combo);
    
    if (candidates.empty()) {
        return AIAction{3, 0, false, true, true, false};
    }
    
    // 5. Valuta ogni candidato
    struct ScoredCandidate {
        PlacementResult result;
        float score;
    };
    std::vector<ScoredCandidate> scored;
    
    // Create spin evaluator with BTB consideration
    SpinEvaluator spinEvaluator(self.btb > 0);
    
    // Check if we have I or T in hold
    bool hasHoldI = (self.hold == PType::I);
    bool hasHoldT = (self.hold == PType::T);
    
    for (auto& c : candidates) {
        float score = 0.0f;
        
        // Danno inflitto
        score += c.damage * 15.0f;
        
        // Qualità del terreno risultante
        score += evaluateTerrainQuality(c.board);
        score += evaluateDoubleDaggerReadiness(c.board);
        
        // Spin evaluation (T-Spin + Tetris)
        SpinType spinType = spinEvaluator.getSpinType(self.board, self.curType, c.x, c.y, c.rot);
        score += spinEvaluator.getScore(spinType, self.btb > 0);
        
        // Additional spin evaluation for the resulting board
        score += spinEvaluator.evaluate(c.board, self.next) * 0.5f;
        
        // New hole evaluation with reachable space analysis
        score += evaluateTerrainWithHoles(c.board, self.next, hasHoldI, hasHoldT);
        
        // Horizontal parity check for perfect clear possibility
        int hParity = calculateHorizontalParity(c.board);
        if (hParity % 4 == 1 || hParity % 4 == 3) {
            score -= EvalWeights::PARITY_PENALTY;
        }
        
        // Bonus memoria pattern
        Aspect newAspect = extractAspect(c.board, 0, self.combo, self.next);
        PatternNeuron* match = state.patternMemory.findBestMatch(newAspect);
        if (match && match->confidence > 2) {
            score += match->confidence * 5.0f;
        }
        
        // Penalità per hold (se non necessario)
        if (c.usedHold) score -= 2.0f;
        
        scored.push_back({c, score});
    }
    
    // 6. Ordina per punteggio
    std::sort(scored.begin(), scored.end(),
              [](const ScoredCandidate& a, const ScoredCandidate& b) {
                  return a.score > b.score;
              });
    
    // 7. Se il migliore è un nuovo pattern interessante, memorizzalo
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
    
    // 8. Restituisci l'azione migliore
    return makeAction(scored[0].result, scored[0].result.usedHold);
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

// ---- Esecuzione azione con DAS/ARR ----
void executeAI(PlayerState& ps, AIAction& act, double dt, double das, double arr) {
    if (!act.ready) {
        ps.moveRepeat = {};
        return;
    }
    
    // Hold
    if (act.shouldHold && !act.holdDone) {
        holdPiece(ps);
        act.holdDone = true;
        return;
    }
    
    // Rotazione
    if (ps.curRot != act.targetRot) {
        Piece test = {ps.curType, ps.curX, ps.curY, ps.curRot, false};
        test.rotate(ps.board, 1);
        ps.curX = test.x;
        ps.curY = test.y;
        ps.curRot = test.rot;
        return;
    }
    
    // Movimento laterale
    if (ps.curX != act.targetX) {
        int dir = (ps.curX < act.targetX) ? 1 : -1;
        applyMoveRepeat(ps, dir, dt, das, arr);
        return;
    }
    
    // Hard drop
    ps.moveRepeat = {};
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
