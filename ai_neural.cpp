// ===================================================================
// ai_neural.cpp - 予測ニューロン型AI実装
// ===================================================================
#include "ai_neural.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <numeric>

// ============================================================
// 1. 20段階観測の生成
// ============================================================

std::array<float, 20> generateObservation(const BoardBits& board, const std::deque<PType>& next, int combo, float gameTime) {
    std::array<float, 20> obs{};
    int idx = 0;
    
    // 段1-10: 各列の高さ（正規化）
    for (int c = 0; c < 10; ++c) {
        obs[idx++] = getColumnHeight(board, c) / 20.0f;
    }
    
    // 段11: 積み高さ
    int stackHeight = 0;
    for (int r = 0; r < BOARD_H; ++r) {
        if (board[r] != 0) { stackHeight = BOARD_H - r; break; }
    }
    obs[idx++] = stackHeight / 20.0f;
    
    // 段12: 穴の数（正規化）
    obs[idx++] = countHoles(board) / 20.0f;
    
    // 段13: パリティ
    obs[idx++] = calculateHorizontalParity(board) / 20.0f;
    
    // 段14: 完成ライン数（最近3手分）
    obs[idx++] = 0.0f;  // 履歴から取得（外部で設定）
    
    // 段15-17: 次のミノ（I, T, その他）
    bool hasI = false, hasT = false;
    for (PType p : next) {
        if (p == PType::I) hasI = true;
        if (p == PType::T) hasT = true;
    }
    obs[idx++] = hasI ? 1.0f : 0.0f;
    obs[idx++] = hasT ? 1.0f : 0.0f;
    obs[idx++] = (hasI || hasT) ? 0.0f : 1.0f;
    
    // 段18: コンボ
    obs[idx++] = combo / 20.0f;
    
    // 段19: ダメージポテンシャル（仮）
    obs[idx++] = 0.0f;
    
    // 段20: タイミング
    obs[idx++] = gameTime / 60.0f;
    
    return obs;
}

// ============================================================
// 5. 相手ミノ有利判定
// ============================================================

PredictionNeuron::OpponentAdvantage analyzeOpponentAdvantage(const std::deque<PType>& next, PType hold) {
    PredictionNeuron::OpponentAdvantage adv;
    adv.hasI = false;
    adv.hasT = false;
    adv.hasLJ = false;
    adv.nextPieceCount = next.size();
    
    for (PType p : next) {
        if (p == PType::I) adv.hasI = true;
        if (p == PType::T) adv.hasT = true;
        if (p == PType::L || p == PType::J) adv.hasLJ = true;
    }
    if (hold == PType::I) adv.hasI = true;
    if (hold == PType::T) adv.hasT = true;
    if (hold == PType::L || hold == PType::J) adv.hasLJ = true;
    
    adv.advantageScore = 0.0f;
    if (adv.hasI) adv.advantageScore += 0.3f;
    if (adv.hasT) adv.advantageScore += 0.4f;
    if (adv.hasLJ) adv.advantageScore += 0.2f;
    if (adv.nextPieceCount >= 3) adv.advantageScore += 0.1f;
    
    return adv;
}

// ============================================================
// 6. ニューロン生成
// ============================================================

PredictionNeuron createPredictionNeuron(const BoardBits& board, const std::deque<PType>& next, PType hold, int combo, float gameTime) {
    PredictionNeuron neuron;
    
    neuron.rawObservation = generateObservation(board, next, combo, gameTime);
    neuron.oppTerrain = analyzeOpponentTerrain(board);
    neuron.tSetup = analyzeTSetup(board);
    neuron.oppAdvantage = analyzeOpponentAdvantage(next, hold);
    
    neuron.hiddenLayer1.resize(32, 0.0f);
    neuron.hiddenLayer2.resize(16, 0.0f);
    neuron.hiddenLayer3.resize(8, 0.0f);
    
    neuron.prediction.nextBoardSimilarity = 0.5f;
    neuron.prediction.chainContinuation = 0.5f;
    neuron.prediction.damagePotential = 0.0f;
    neuron.prediction.timingFactor = 0.5f;
    neuron.prediction.opponentReaction = 0.5f;
    
    std::vector<float> hashVec;
    for (float v : neuron.rawObservation) hashVec.push_back(v);
    neuron.lshHash = lshHash(hashVec);
    
    neuron.pairMemory.selfPair.relation = 1.0f;
    neuron.pairMemory.oppPair.relation = 1.0f;
    neuron.pairMemory.timing.relation = 0.5f;
    
    return neuron;
}

// ============================================================
// 7. 類似度計算
// ============================================================

float PredictionNeuron::similarity(const PredictionNeuron& other) const {
    float score = 0.0f;
    int count = 0;
    
    // 20段階観測（重み: 0.4）
    for (int i = 0; i < 20; ++i) {
        score += 1.0f - std::abs(rawObservation[i] - other.rawObservation[i]);
        count++;
    }
    
    // 相手地形（重み: 0.2）
    if (oppTerrain.tallerSide == other.oppTerrain.tallerSide) {
        score += 0.2f;
    }
    if (oppTerrain.canFillFlatFromFirst == other.oppTerrain.canFillFlatFromFirst) {
        score += 0.1f;
    }
    if (oppTerrain.canFillFlatFromSecond == other.oppTerrain.canFillFlatFromSecond) {
        score += 0.1f;
    }
    count += 4;
    
    // TST/TSD準備（重み: 0.2）
    float tDiff = std::abs(tSetup.tSetupScore - other.tSetup.tSetupScore);
    score += 0.2f * (1.0f - tDiff);
    count += 1;
    
    // 相手ミノ有利（重み: 0.2）
    float advDiff = std::abs(oppAdvantage.advantageScore - other.oppAdvantage.advantageScore);
    score += 0.2f * (1.0f - advDiff);
    count += 1;
    
    return score / count;
}

// ============================================================
// 8. ニューロン活性化
// ============================================================

int activateNeuron(NeuralAIState& state, const PredictionNeuron& current) {
    int bestIdx = -1;
    float bestSim = state.similarityThreshold;
    
    for (size_t i = 0; i < state.neurons.size(); ++i) {
        float sim = current.similarity(state.neurons[i]);
        if (sim > bestSim) {
            bestSim = sim;
            bestIdx = i;
        }
    }
    
    if (bestIdx >= 0) {
        state.activeNeuronIndex = bestIdx;
    } else {
        state.neurons.push_back(current);
        state.activeNeuronIndex = state.neurons.size() - 1;
        if (state.autoExpandLayers) {
            // 誤差がないのでデフォルトで拡張
            PredictionNeuron::PredictedValues defaultError;
            defaultError.nextBoardSimilarity = 0.3f;
            defaultError.chainContinuation = 0.3f;
            defaultError.damagePotential = 0.3f;
            defaultError.timingFactor = 0.3f;
            defaultError.opponentReaction = 0.3f;
            autoExpandLayers(state, current, defaultError);
        }
    }
    
    return state.activeNeuronIndex;
}

// ============================================================
// 9. 予測実行
// ============================================================

PredictionNeuron::PredictedValues predictNext(const PredictionNeuron& current, const PredictionNeuron& target) {
    PredictionNeuron::PredictedValues pred;
    float sim = current.similarity(target);
    
    pred.nextBoardSimilarity = sim;
    pred.chainContinuation = std::min(1.0f, sim * 1.2f);
    pred.damagePotential = current.tSetup.tSetupScore * 0.4f +
                           current.oppAdvantage.advantageScore * 0.3f +
                           pred.chainContinuation * 0.3f;
    
    float timeFactor = std::sin(current.rawObservation[19] * 3.14f);
    pred.timingFactor = 0.5f + timeFactor * 0.3f;
    pred.opponentReaction = current.oppTerrain.canFillFlatFromFirst ? 0.8f : 0.3f;
    
    return pred;
}

// ============================================================
// 10. ペア記憶
// ============================================================

void storePairMemory(PredictionNeuron& neuron, const VariantSignature& selfBefore, const VariantSignature& selfAfter, const VariantSignature& oppNext, float fireStartTime, float fireCreatedTime, float fireSentTime) {
    neuron.pairMemory.selfPair.before = selfBefore;
    neuron.pairMemory.selfPair.after = selfAfter;
    neuron.pairMemory.selfPair.relation = 1.0f;
    
    neuron.pairMemory.oppPair.selfCurrent = selfBefore;
    neuron.pairMemory.oppPair.oppNext = oppNext;
    neuron.pairMemory.oppPair.relation = 1.0f;
    
    neuron.pairMemory.timing.fireStart = fireStartTime;
    neuron.pairMemory.timing.fireCreated = fireCreatedTime;
    neuron.pairMemory.timing.fireSent = fireSentTime;
    
    float timeDiff = std::abs(fireStartTime - fireCreatedTime);
    neuron.pairMemory.timing.relation = std::max(0.0f, 1.0f - timeDiff * 0.1f);
}

// ============================================================
// 11. 自律的層拡張（予測不十分要素を学習）
// ============================================================

void autoExpandLayers(NeuralAIState& state, const PredictionNeuron& current, const PredictionNeuron::PredictedValues& error) {
    struct LearningTarget {
        std::string feature;
        float errorMagnitude;
        int layerIndex;
    };
    std::vector<LearningTarget> targets;
    
    if (error.nextBoardSimilarity > 0.3f) {
        targets.push_back({"boardSimilarity", error.nextBoardSimilarity, 0});
    }
    if (error.chainContinuation > 0.3f) {
        targets.push_back({"chainContinuation", error.chainContinuation, 1});
    }
    if (error.damagePotential > 0.3f) {
        targets.push_back({"damagePotential", error.damagePotential, 2});
    }
    if (error.timingFactor > 0.3f) {
        targets.push_back({"timingFactor", error.timingFactor, 3});
    }
    if (error.opponentReaction > 0.3f) {
        targets.push_back({"opponentReaction", error.opponentReaction, 4});
    }
    
    for (const auto& target : targets) {
        PredictionNeuron newNeuron = current;
        switch (target.layerIndex) {
            case 0: newNeuron.hiddenLayer1.push_back(target.errorMagnitude); break;
            case 1: newNeuron.hiddenLayer2.push_back(target.errorMagnitude); break;
            case 2: newNeuron.hiddenLayer3.push_back(target.errorMagnitude); break;
            case 3:
                newNeuron.hiddenLayer1.push_back(target.errorMagnitude * 0.5f);
                newNeuron.hiddenLayer2.push_back(target.errorMagnitude * 0.5f);
                break;
            case 4:
                newNeuron.hiddenLayer2.push_back(target.errorMagnitude);
                newNeuron.hiddenLayer3.push_back(target.errorMagnitude);
                break;
        }
        state.neurons.push_back(newNeuron);
    }
    
    state.layerStructure.clear();
    state.layerStructure.push_back(20);
    if (!state.neurons.empty()) {
        state.layerStructure.push_back(state.neurons[0].hiddenLayer1.size());
        if (!state.neurons[0].hiddenLayer2.empty()) {
            state.layerStructure.push_back(state.neurons[0].hiddenLayer2.size());
        }
        if (!state.neurons[0].hiddenLayer3.empty()) {
            state.layerStructure.push_back(state.neurons[0].hiddenLayer3.size());
        }
    }
    
    if (state.neurons.size() > state.maxNeurons) {
        state.neurons.erase(state.neurons.begin());
    }
}

// ============================================================
// 12. メインAI決定
// ============================================================

AIAction decideNeuralAI(NeuralAIState& state, PlayerState& self, PlayerState& opp, float gameTime, float fireStartTime, float fireCreatedTime, float fireSentTime) {
    state.totalPredictions++;
    
    // 現在のニューロン生成
    PredictionNeuron current = createPredictionNeuron(self.board, self.next, self.hold, self.combo, gameTime);
    current.rawObservation[14] = self.linesCleared / 10.0f;
    
    // ニューロン活性化
    int idx = activateNeuron(state, current);
    if (idx < 0) {
        return AIAction{3, 0, false, true, true, false};
    }
    
    auto& active = state.neurons[idx];
    active.prediction = predictNext(current, active);
    
    // 行動決定
    AIAction act;
    act.ready = true;
    act.shouldDrop = true;
    act.shouldHold = false;
    
    // TST/TSD準備がある場合
    if (active.tSetup.tSetupCount > 0) {
        for (int rot = 0; rot < 4; ++rot) {
            const MinoShape& shape = SHAPES[(int)PType::T][rot];
            for (int x = -2; x < BOARD_W + 2; ++x) {
                if (IsCollision(self.board, shape, x, 0)) continue;
                int y = HardDropY(self.board, shape, x);
                if (y < 0) continue;
                if (isTSpin(self.board, x, y, rot)) {
                    act.targetX = x;
                    act.targetRot = rot;
                    
                    VariantSignature selfBefore = recognizeVariant(self.board, self.next);
                    BoardBits tempBoard = self.board;
                    for (int r = 0; r < shape.height; ++r) {
                        int row = y + r;
                        if (row < 0 || row >= BOARD_H) continue;
                        uint16_t mask = shape.rows[r];
                        if (x >= 0) mask <<= x;
                        else mask >>= (-x);
                        tempBoard[row] |= mask;
                    }
                    VariantSignature selfAfter = recognizeVariant(tempBoard, self.next);
                    VariantSignature oppNext = recognizeVariant(opp.board, opp.next);
                    storePairMemory(active, selfBefore, selfAfter, oppNext, fireStartTime, fireCreatedTime, fireSentTime);
                    
                    return act;
                }
            }
        }
    }
    
    // 平らに埋められる場合
    if (active.oppTerrain.canFillFlatFromFirst) {
        for (int rot = 0; rot < 4; ++rot) {
            const MinoShape& shape = SHAPES[(int)PType::I][rot];
            for (int x = -2; x < BOARD_W + 2; ++x) {
                if (IsCollision(self.board, shape, x, 0)) continue;
                int y = HardDropY(self.board, shape, x);
                if (y >= 0) {
                    act.targetX = x;
                    act.targetRot = rot;
                    active.pairMemory.timing.relation *= state.timingDecay;
                    return act;
                }
            }
        }
    }
    
    // 相手有利な場合
    if (active.oppAdvantage.advantageScore > 0.5f) {
        auto candidates = EnumerateAllPlacements(self.board, self.curType, self.canHold, self.hold, self.btb, self.combo);
        if (!candidates.empty()) {
            int bestDamage = -1;
            PlacementResult* best = nullptr;
            for (auto& c : candidates) {
                if (c.damage > bestDamage) {
                    bestDamage = c.damage;
                    best = &c;
                }
            }
            if (best) {
                act.targetX = best->x;
                act.targetRot = best->rot;
                act.shouldHold = best->usedHold;
                return act;
            }
        }
    }
    
    // フォールバック
    auto candidates = EnumerateAllPlacements(self.board, self.curType, self.canHold, self.hold, self.btb, self.combo);
    if (!candidates.empty()) {