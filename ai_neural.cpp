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
// ===================================================================
// ai_core.cpp - 状況依存バリアント・チェーンAI 実装
// ===================================================================
#include "ai_core.h"
#include "ai_evaluate.h"
#include "pc_parity.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>

// ============================================================
// VariantSignature 実装
// ============================================================

float VariantSignature::similarity(const VariantSignature& other) const {
    // 1. 地形パターン（重み: 0.5）
    int topRowDiff = (topRows ^ other.topRows).count();
    float topSimilarity = 1.0f - (static_cast<float>(topRowDiff) / 30.0f);
    
    // 2. 特徴ベクトル（重み: 0.3）
    float vecSimilarity = 0.0f;
    if (!featureVector.empty() && !other.featureVector.empty()) {
        float dot = 0.0f, normA = 0.0f, normB = 0.0f;
        size_t n = std::min(featureVector.size(), other.featureVector.size());
        for (size_t i = 0; i < n; ++i) {
            dot += featureVector[i] * other.featureVector[i];
            normA += featureVector[i] * featureVector[i];
            normB += other.featureVector[i] * other.featureVector[i];
        }
        if (normA > 0 && normB > 0) {
            vecSimilarity = dot / (std::sqrt(normA) * std::sqrt(normB));
        }
    }
    
    // 3. コンテキスト（重み: 0.2）
    float contextSimilarity = 0.0f;
    float heightDiff = std::abs(stackHeight - other.stackHeight) / 20.0f;
    float holeDiff = std::abs(holes - other.holes) / 20.0f;
    float parityMatch = (oddParityRows == other.oddParityRows) ? 1.0f : 0.0f;
    float wellMatch = (hasWell == other.hasWell) ? 1.0f : 0.0f;
    contextSimilarity = (1.0f - heightDiff) * 0.3f + 
                        (1.0f - holeDiff) * 0.3f +
                        parityMatch * 0.2f +
                        wellMatch * 0.2f;
    
    return topSimilarity * 0.5f + vecSimilarity * 0.3f + contextSimilarity * 0.2f;
}

// ============================================================
// 状況認識
// ============================================================

VariantSignature recognizeVariant(const BoardBits& board, const std::deque<PType>& next) {
    VariantSignature sig;
    
    // 地形パターン（上3行）
    sig.topRows = GetTop3Rows(board);
    
    // 特徴ベクトル抽出
    Aspect aspect = extractAspect(board);
    sig.featureVector = aspect.values;
    if (sig.featureVector.size() > 20) {
        sig.featureVector.resize(20);
    }
    
    // LSHハッシュ
    sig.lshHash = lshHash(sig.featureVector);
    
    // コンテキスト情報
    sig.stackHeight = 0;
    for (int r = 0; r < BOARD_H; ++r) {
        if (board[r] != 0) { sig.stackHeight = BOARD_H - r; break; }
    }
    
    sig.holes = countHoles(board);
    sig.oddParityRows = calculateHorizontalParity(board);
    
    // Well（縦穴）の有無
    sig.hasWell = false;
    for (int c = 0; c < BOARD_W; ++c) {
        int wellDepth = 0;
        int h = getColumnHeight(board, c);
        for (int r = h; r < BOARD_H; ++r) {
            if (!(board[r] & (1 << c))) {
                wellDepth++;
            } else {
                break;
            }
        }
        if (wellDepth >= 4) {
            sig.hasWell = true;
            break;
        }
    }
    
    // コンボ可能性（簡易）
    sig.comboPotential = 0;
    for (int r = 0; r < BOARD_H; ++r) {
        if (board[r] == 0x3FF) sig.comboPotential++;
    }
    
    return sig;
}

// ============================================================
// チェーン検索
// ============================================================

int findMatchingChain(const AIState& state, const VariantSignature& current) {
    int bestIdx = -1;
    float bestScore = state.similarityThreshold;
    
    for (size_t i = 0; i < state.chains.size(); ++i) {
        const auto& chain = state.chains[i];
        if (chain.transitions.empty()) continue;
        
        // チェーンの最初の遷移と比較
        const auto& first = chain.transitions[0];
        float sim = current.similarity(first.from);
        
        // コンテキスト条件チェック
        bool heightOk = current.stackHeight >= chain.minStackHeight &&
                       current.stackHeight <= chain.maxStackHeight;
        
        if (sim > bestScore && heightOk) {
            bestScore = sim;
            bestIdx = i;
        }
    }
    
    return bestIdx;
}

// ============================================================
// 遷移選択
// ============================================================

VariantTransition selectTransition(const VariantChain& chain, const std::vector<PType>& hand) {
    // 現在のインデックスが範囲外なら最初に戻る
    int idx = chain.currentIndex;
    if (idx < 0 || idx >= static_cast<int>(chain.transitions.size())) {
        idx = 0;
    }
    
    const auto& transition = chain.transitions[idx];
    
    // 必要なミノが手札にあるかチェック
    if (transition.requiredPiece != PType::I) { // Iはデフォルト（指定なし）
        bool hasRequired = false;
        for (PType p : hand) {
            if (p == transition.requiredPiece) {
                hasRequired = true;
                break;
            }
        }
        if (!hasRequired) {
            // 類似遷移を探す（代替案）
            for (const auto& alt : chain.transitions) {
                if (alt != transition && alt.confidence > 0.5f) {
                    // 次の遷移を試す
                    return alt;
                }
            }
        }
    }
    
    return transition;
}

// ============================================================
// 遷移実行
// ============================================================

AIAction executeTransition(const VariantTransition& transition) {
    AIAction act;
    act.targetX = transition.targetX;
    act.targetRot = transition.targetRot;
    act.shouldHold = transition.useHold;
    act.shouldDrop = true;
    act.ready = true;
    act.holdDone = false;
    return act;
}

// ============================================================
// 予測評価
// ============================================================

float evaluatePrediction(const VariantSignature& predicted, const VariantSignature& actual) {
    float sim = predicted.similarity(actual);
    // 地形パターンが一致しているか（より厳格）
    int diff = (predicted.topRows ^ actual.topRows).count();
    float topMatch = 1.0f - (static_cast<float>(diff) / 30.0f);
    
    // 総合スコア（予測と実際の一致度）
    return sim * 0.6f + topMatch * 0.4f;
}

// ============================================================
// 見誤り分析
// ============================================================

Misperception analyzeMisperception(
    const VariantSignature& perceived,
    const VariantTransition& predicted,
    const VariantSignature& actual,
    const std::vector<PType>& hand,
    float timing
) {
    Misperception mis;
    mis.perceived = perceived;
    mis.predicted = predicted;
    mis.actual = actual;
    mis.hand = hand;
    mis.timing = timing;
    mis.learned = false;
    
    // 誤差計算
    mis.predictionError = 1.0f - evaluatePrediction(predicted.to, actual);
    
    // 誤差タイプを分析
    int topDiff = (predicted.to.topRows ^ actual.topRows).count();
    int handDiff = 0;
    for (size_t i = 0; i < std::min(hand.size(), size_t(3)); ++i) {
        // 手札の違いを分析
    }
    
    if (topDiff > 5) {
        mis.errorType = "地形誤認";
    } else if (handDiff > 0) {
        mis.errorType = "手札誤認";
    } else {
        mis.errorType = "タイミング誤認";
    }
    
    // 重要度（誤差が大きいほど重要）
    mis.weight = mis.predictionError * 2.0f;
    
    return mis;
}

// ============================================================
// 見誤りからの学習（新しいバリアント生成）
// ============================================================

void learnFromMisperception(AIState& state, const Misperception& mis) {
    // 1. 記憶に追加
    state.memory.addMisperception(mis);
    state.recentMisperceptions.push_back(mis);
    
    // 2. 見誤りが一定数蓄積したら新しいチェーン生成
    if (state.recentMisperceptions.size() >= 3) {
        // 類似する見誤りをグループ化
        std::vector<Misperception> similar;
        for (const auto& recent : state.recentMisperceptions) {
            if (recent.perceived.similarity(mis.perceived) > 0.6f) {
                similar.push_back(recent);
            }
        }
        
        if (similar.size() >= 2) {
            // 新しいバリアントチェーンを生成
            createNewVariantChain(state, mis);
            state.recentMisperceptions.clear();
        }
    }
    
    // 3. 類似チェーンの合成
    if (state.chains.size() > state.maxChains * 0.8f) {
        mergeSimilarChains(state);
    }
}

// ============================================================
// 新しいバリアントチェーン生成（核心！）
// ============================================================

void createNewVariantChain(AIState& state, const Misperception& mis) {
    VariantChain newChain;
    
    // ユニークID生成
    static int chainId = 0;
    newChain.id = "chain_" + std::to_string(chainId++);
    newChain.name = "Variant from misperception";
    
    // 遷移を構築
    VariantTransition t1, t2;
    
    // t1: 認識した盤面 → 予測した行動
    t1.from = mis.perceived;
    t1.targetX = mis.predicted.targetX;
    t1.targetRot = mis.predicted.targetRot;
    t1.useHold = mis.predicted.useHold;
    t1.requiredPiece = mis.predicted.requiredPiece;
    t1.to = mis.predicted.to;
    t1.confidence = 1.0f - mis.predictionError;
    t1.successCount = 0;
    t1.failCount = 1;
    
    // t2: 予測した盤面 → 実際の結果（修正版）
    t2.from = mis.predicted.to;
    t2.targetX = mis.actualX;
    t2.targetRot = mis.actualRot;
    t2.useHold = false;
    t2.requiredPiece = mis.actualPiece;
    t2.to = mis.actual;
    t2.confidence = 1.0f - mis.predictionError * 0.5f;
    t2.successCount = 1;
    t2.failCount = 0;
    
    newChain.transitions = {t1, t2};
    newChain.currentIndex = 0;
    
    // 発動条件
    newChain.minStackHeight = mis.perceived.stackHeight - 2;
    newChain.maxStackHeight = mis.perceived.stackHeight + 2;
    
    // 必要なミノ
    newChain.requiredPieces = mis.hand;
    
    // 統計
    newChain.activationCount = 1;
    newChain.completionCount = 0;
    newChain.isActive = false;
    newChain.progress = 0.0f;
    
    // チェーンを追加
    state.chains.push_back(newChain);
    state.misperceptionCount++;
}

// ============================================================
// 類似チェーン合成
// ============================================================

void mergeSimilarChains(AIState& state) {
    std::vector<int> toMerge;
    std::unordered_set<int> merged;
    
    for (size_t i = 0; i < state.chains.size(); ++i) {
        if (merged.count(i)) continue;
        
        for (size_t j = i + 1; j < state.chains.size(); ++j) {
            if (merged.count(j)) continue;
            
            const auto& a = state.chains[i];
            const auto& b = state.chains[j];
            
            if (a.transitions.empty() || b.transitions.empty()) continue;
            
            // 最初の遷移の類似度をチェック
            float sim = a.transitions[0].from.similarity(b.transitions[0].from);
            if (sim > 0.8f) {
                // 合成候補
                toMerge.push_back(i);
                toMerge.push_back(j);
                merged.insert(i);
                merged.insert(j);
                break;
            }
        }
    }
    
    // 合成実行（実際のマージ処理）
    for (int idx : toMerge) {
        // 信頼度の高い方にマージ
        // （簡易実装：後ろのチェーンを削除）
        if (idx < static_cast<int>(state.chains.size())) {
            state.chains.erase(state.chains.begin() + idx);
        }
    }
}

// ============================================================
// メインAI決定
// ============================================================

AIAction decideVariantAI(AIState& state, PlayerState& self, PlayerState& opp, float gameTime) {
    state.totalDecisions++;
    
    // 1. 現在の状況を認識
    VariantSignature current = recognizeVariant(self.board, self.next);
    
    // 2. アクティブチェーンの継続
    if (state.activeChainIndex >= 0 && state.activeChainIndex < static_cast<int>(state.chains.size())) {
        auto& chain = state.chains[state.activeChainIndex];
        
        if (chain.currentIndex < static_cast<int>(chain.transitions.size())) {
            // 手札を準備
            std::vector<PType> hand;
            for (PType p : self.next) hand.push_back(p);
            if (self.canHold) hand.push_back(self.hold);
            
            // 次の遷移を選択
            VariantTransition nextTrans = selectTransition(chain, hand);
            
            // 遷移が有効かチェック
            if (current.similarity(nextTrans.from) > 0.5f) {
                // 実行
                auto act = executeTransition(nextTrans);
                chain.currentIndex++;
                chain.progress = static_cast<float>(chain.currentIndex) / chain.transitions.size();
                
                if (chain.currentIndex >= static_cast<int>(chain.transitions.size())) {
                    chain.isActive = false;
                    chain.completionCount++;
                    state.activeChainIndex = -1;
                }
                
                return act;
            } else {
                // 遷移が無効になった
                chain.isActive = false;
                state.activeChainIndex = -1;
            }
        }
    }
    
    // 3. 新しいチェーンを検索
    int matchIdx = findMatchingChain(state, current);
    if (matchIdx >= 0) {
        state.activeChainIndex = matchIdx;
        auto& chain = state.chains[matchIdx];
        chain.isActive = true;
        chain.currentIndex = 0;
        chain.activationCount++;
        
        // 再帰的に実行（最初の遷移）
        return decideVariantAI(state, self, opp, gameTime);
    }
    
    // 4. 類似経験をメモリから検索
    auto similarExps = state.memory.findSimilar(current, 5);
    if (!similarExps.empty()) {
        // 最も近い経験の行動を再利用
        const auto& best = similarExps[0];
        auto act = executeTransition(best.action);
        
        // ただし、今回は見誤りとしてマーク（後で学習用）
        // 実際の結果は後で評価される
        return act;
    }
    
    // 5. フォールバック：従来の評価関数を使用
    // （ここでは簡易的にテンプレートマッチング）
    auto candidates = EnumerateAllPlacements(self.board, self.curType,
                                              self.canHold, self.hold,
                                              self.btb, self.combo);
    
    if (candidates.empty()) {
        return AIAction{3, 0, false, true, true, false};
    }
    
    // 最も高い評価のものを選択
    float bestScore = -1e9;
    PlacementResult* best = nullptr;
    for (auto& c : candidates) {
        float score = evaluateTerrainQuality(c.board) +
                      c.damage * 10.0f;
        if (score > bestScore) {
            bestScore = score;
            best = &c;
        }
    }
    
    if (best) {
        AIAction act;
        act.targetX = best->x;
        act.targetRot = best->rot;
        act.shouldHold = best->usedHold;
        act.shouldDrop = true;
        act.ready = true;
        return act;
    }
    
    return AIAction{3, 0, false, true, true, false};
}

// ============================================================
// NeuralMemory 実装
// ============================================================

void NeuralMemory::addExperience(const Experience& exp) {
    experiences.push_back(exp);
    
    // LSHインデックス更新
    uint64_t hash = lshHash(exp.state.featureVector);
    lshIndex[hash].push_back(experiences.size() - 1);
    
    if (experiences.size() > MAX_EXPERIENCES) {
        pruneOldest();
    }
}

void NeuralMemory::addMisperception(const Misperception& mis) {
    misperceptions.push_back(mis);
    if (misperceptions.size() > MAX_MISPERCEPTIONS) {
        // 最も古いものを削除（重みが低いものから）
        std::sort(misperceptions.begin(), misperceptions.end(),
                  [](const Misperception& a, const Misperception& b) {
                      return a.weight < b.weight;
                  });
        misperceptions.erase(misperceptions.begin());
    }
}

std::vector<NeuralMemory::Experience> NeuralMemory::findSimilar(
    const VariantSignature& query, int topK) {
    
    std::vector<Experience> results;
    
    // LSHで候補を絞る
    uint64_t queryHash = lshHash(query.featureVector);
    std::unordered_set<size_t> candidates;
    
    for (const auto& [hash, indices] : lshIndex) {
        if ((hash ^ queryHash) < (1ULL << 10)) {
            for (size_t idx : indices) {
                candidates.insert(idx);
            }
        }
    }
    
    // 各候補の類似度を計算
    std::vector<std::pair<float, size_t>> scored;
    for (size_t idx : candidates) {
        if (idx < experiences.size()) {
            float sim = experiences[idx].state.similarity(query);
            if (sim > 0.5f) {
                scored.push_back({sim, idx});
            }
        }
    }
    
    // スコア順にソート
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    // トップKを取得
    int count = 0;
    for (const auto& [score, idx] : scored) {
        if (count >= topK) break;
        results.push_back(experiences[idx]);
        count++;
    }
    
    return results;
}

std::vector<Misperception> NeuralMemory::findRecentMisperceptions(int count) {
    std::vector<Misperception> recent;
    int start = std::max(0, static_cast<int>(misperceptions.size()) - count);
    for (int i = start; i < static_cast<int>(misperceptions.size()); ++i) {
        recent.push_back(misperceptions[i]);
    }
    return recent;
}

void NeuralMemory::pruneOldest() {
    if (experiences.size() <= MAX_EXPERIENCES) return;
    
    // 最も古い経験を削除
    experiences.erase(experiences.begin());
    
    // インデックス再構築（簡易）
    lshIndex.clear();
    for (size_t i = 0; i < experiences.size(); ++i) {
        uint64_t hash = lshHash(experiences[i].state.featureVector);
        lshIndex[hash].push_back(i);
    }
}

// ============================================================
// ユーティリティ
// ============================================================

float variantDistance(const VariantSignature& a, const VariantSignature& b) {
    return 1.0f - a.similarity(b);
}

uint64_t computeVariantLSH(const VariantSignature& sig) {
    return lshHash(sig.featureVector);
}

std::string chainToString(const VariantChain& chain) {
    std::string s = "Chain: " + chain.name + " (ID: " + chain.id + ")\n";
    s += "  Transitions: " + std::to_string(chain.transitions.size()) + "\n";
    s += "  Success rate: " + std::to_string(chain.overallSuccessRate()) + "\n";
    s += "  Active: " + std::to_string(chain.isActive) + "\n";
    s += "  Progress: " + std::to_string(chain.progress) + "\n";
    return s;
}

void saveChains(const std::vector<VariantChain>& chains, const std::string& path) {
    // JSONなどで保存（簡易実装）
    // 実際はバイナリまたはJSONで保存
}

void loadChains(std::vector<VariantChain>& chains, const std::string& path) {
    // 保存ファイルから読み込み
}
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