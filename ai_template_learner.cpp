// ===================================================================
// ai_template_learner.cpp - 局所テンプレート自動記憶学習器 実装
// ===================================================================
//
// 探索方針:
//   種地形 (LocalPattern) を盤面上の絶対位置 (originX, originY) に置いた
//   本物の BoardBits を初期状態とし、ai_core.cpp の beamSearch と同じ骨格
//   (EnumerateAllPlacements を呼び、ボード/ホールド/btb/combo を引き継ぎながら
//    深さを進め、各深さでビーム幅に絞る) で探索する。
//
//   注目領域の外を経由する配置やライン消去も許可する。
//   成功条件は「注目領域 (originX, originY, width, height) が完全に埋まっている」
//   かつ「注目領域の外側に、探索開始時点よりも新しい閉じた穴 (到達不能な空きマス)
//   を作っていない」の両方を満たすこと。
//
#include "ai_template_learner.h"
#include "ai_evaluate.h"
#include <algorithm>
#include <numeric>

// ============================================================
// LocalPattern <-> BoardBits 変換
// ============================================================

LocalPattern ExtractLocalPattern(const BoardBits& board, int offsetX, int offsetY,
                                  int width, int height) {
    LocalPattern pat(width, height);
    for (int y = 0; y < height; ++y) {
        int boardRow = offsetY + y;
        if (boardRow < 0 || boardRow >= static_cast<int>(board.size())) continue;
        uint16_t row = board[boardRow];
        for (int x = 0; x < width; ++x) {
            int boardCol = offsetX + x;
            if (boardCol < 0 || boardCol >= BOARD_W) continue;
            if (row & (1 << boardCol)) pat.set(x, y, true);
        }
    }
    return pat;
}

void StampLocalPattern(BoardBits& board, const LocalPattern& pat, int offsetX, int offsetY) {
    for (int y = 0; y < pat.height; ++y) {
        int boardRow = offsetY + y;
        if (boardRow < 0 || boardRow >= static_cast<int>(board.size())) continue;
        for (int x = 0; x < pat.width; ++x) {
            if (!pat.get(x, y)) continue;
            int boardCol = offsetX + x;
            if (boardCol < 0 || boardCol >= BOARD_W) continue;
            board[boardRow] |= static_cast<uint16_t>(1u << boardCol);
        }
    }
}

// ============================================================
// SlotMinoStats
// ============================================================

std::vector<PType> SlotMinoStats::requiredMinos() const {
    // 「成立した試行の中で、このミノ種以外では一度も成立しなかった」ミノを必須とする。
    std::vector<PType> successfulTypes;
    for (int i = 0; i < NUM_PTYPES; ++i) {
        if (successCount[i] > 0) successfulTypes.push_back(static_cast<PType>(i));
    }
    if (successfulTypes.size() == 1) return successfulTypes;
    return {};
}

std::vector<std::pair<PType, float>> SlotMinoStats::preferredMinos(float minSuccessRate) const {
    std::vector<std::pair<PType, float>> out;
    for (int i = 0; i < NUM_PTYPES; ++i) {
        if (attemptCount[i] <= 0) continue;
        float rate = static_cast<float>(successCount[i]) / static_cast<float>(attemptCount[i]);
        if (successCount[i] > 0 && rate >= minSuccessRate) {
            out.push_back({static_cast<PType>(i), rate});
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    return out;
}

// ============================================================
// バッグ順列の生成
// ============================================================

std::vector<std::vector<PType>> GenerateBagPermutations(int maxLen) {
    std::vector<PType> bag = {PType::I, PType::O, PType::T, PType::S,
                               PType::Z, PType::J, PType::L};
    if (maxLen <= 0 || maxLen > 7) maxLen = 7;

    std::sort(bag.begin(), bag.end(), [](PType a, PType b) {
        return static_cast<int>(a) < static_cast<int>(b);
    });

    std::vector<std::vector<PType>> results;
    std::vector<PType> current = bag;
    do {
        std::vector<PType> seq(current.begin(), current.begin() + maxLen);
        results.push_back(seq);
    } while (std::next_permutation(current.begin(), current.end(), [](PType a, PType b) {
        return static_cast<int>(a) < static_cast<int>(b);
    }));

    return results;
}

// ============================================================
// TemplateLearner
// ============================================================

int TemplateLearner::addSeed(const std::string& name, const LocalPattern& seed,
                              int originX, int originY) {
    LearnedTemplate t;
    t.name = name;
    t.seedPattern = seed;
    t.width = seed.width;
    t.height = seed.height;
    t.originX = originX;
    t.originY = originY;
    templates.push_back(std::move(t));
    return static_cast<int>(templates.size()) - 1;
}

std::vector<int> TemplateLearner::pendingTemplateIndices() const {
    std::vector<int> pending;
    for (size_t i = 0; i < templates.size(); ++i) {
        if (!templates[i].failedSequences.empty() ||
            templates[i].totalSequencesTried == 0) {
            pending.push_back(static_cast<int>(i));
        }
    }
    return pending;
}

namespace {

// 注目領域 [originX, originX+width) x [originY, originY+height) が
// 完全に埋まっているか。
bool RegionFilled(const BoardBits& board, int originX, int originY, int width, int height) {
    for (int y = originY; y < originY + height; ++y) {
        if (y < 0 || y >= static_cast<int>(board.size())) return false;
        for (int x = originX; x < originX + width; ++x) {
            if (x < 0 || x >= BOARD_W) return false;
            if (!(board[y] & (1u << x))) return false;
        }
    }
    return true;
}

// 注目領域の「外側」にある閉じた穴 (到達不能な空きマス) の数を数える。
// countHoles() は列単位の単純な被覆判定 (盤面全体) なので、
// 注目領域内のマスを除外してカウントする軽量版として実装する。
int CountHolesExcludingRegion(const BoardBits& board, int originX, int originY,
                               int width, int height) {
    int holes = 0;
    for (int c = 0; c < BOARD_W; ++c) {
        bool foundBlock = false;
        for (int r = 0; r < BOARD_H; ++r) {
            bool inRegion = (c >= originX && c < originX + width &&
                              r >= originY && r < originY + height);
            bool filled = (board[r] & (1u << c)) != 0;
            if (filled) {
                foundBlock = true;
            } else if (foundBlock && !inRegion) {
                holes++;
            }
        }
    }
    return holes;
}

// ---- beamSearch 相当のノード ----
struct LearnBeamNode {
    BoardBits board;
    std::vector<PType> remaining;   // 残りミノ列 (先頭から消費)
    PType hold = PType::I;
    bool holdFilled = false;
    bool canHold = true;

    // ここまでの手順で使ったスロット (領域基準の相対座標) とミノ種
    std::vector<TemplateSlot> slots;
    std::vector<PType> used;

    bool success = false;
};

} // namespace

bool TemplateLearner::trySequence(const LearnedTemplate& tmpl,
                                   const std::vector<PType>& sequence,
                                   bool allowHold,
                                   size_t nodeLimit,
                                   std::vector<TemplateSlot>& outSlots,
                                   std::vector<PType>& outUsedPieces) const {
    outSlots.clear();
    outUsedPieces.clear();

    // 初期盤面: 種地形を絶対位置 (originX, originY) に置いただけの、他は空の盤面。
    // (実戦では周囲に既存の地形があるが、テンプレート学習の第一段階としては
    //  「注目領域の外側は初期状態では空」という前提で、領域外を自由に経由できる
    //  かどうかを学習する。)
    BoardBits initBoard{};
    StampLocalPattern(initBoard, tmpl.seedPattern, tmpl.originX, tmpl.originY);

    int baseHolesOutside = CountHolesExcludingRegion(initBoard, tmpl.originX, tmpl.originY,
                                                       tmpl.width, tmpl.height);

    LearnBeamNode root;
    root.board = initBoard;
    root.remaining = sequence;
    root.canHold = allowHold;

    std::vector<LearnBeamNode> beam;
    beam.push_back(std::move(root));

    size_t nodesVisited = 0;
    const int beamWidth = 30;

    for (int depth = 0; depth < static_cast<int>(sequence.size()); ++depth) {
        std::vector<LearnBeamNode> nextBeam;
        nextBeam.reserve(beam.size() * 4);

        for (auto& parent : beam) {
            if (parent.success) {
                nextBeam.push_back(parent);
                continue;
            }
            if (parent.remaining.empty()) {
                nextBeam.push_back(parent);
                continue;
            }

            PType curType = parent.remaining.front();
            std::vector<PType> restAfterCur(parent.remaining.begin() + 1, parent.remaining.end());

            // ホールド候補: 現在ホールドに入っているミノ、または未使用なら次のミノを先出し。
            PType holdCandidate = PType::COUNT;
            if (parent.canHold) {
                if (parent.holdFilled) {
                    holdCandidate = parent.hold;
                } else if (!restAfterCur.empty()) {
                    holdCandidate = restAfterCur.front();
                }
            }

            // ---- 候補A: 現在のミノをそのまま置く ----
            {
                auto placements = EnumerateAllPlacements(parent.board, curType,
                                                          false, PType::I, 0, 0);
                for (auto& p : placements) {
                    if (nodesVisited++ > nodeLimit) goto search_limit_reached;

                    LearnBeamNode child;
                    child.board = p.board;
                    child.remaining = restAfterCur;
                    child.hold = parent.hold;
                    child.holdFilled = parent.holdFilled;
                    child.canHold = allowHold;
                    child.slots = parent.slots;
                    child.slots.push_back(TemplateSlot{p.x - tmpl.originX, p.y - tmpl.originY, p.rot});
                    child.used = parent.used;
                    child.used.push_back(curType);

                    if (RegionFilled(child.board, tmpl.originX, tmpl.originY, tmpl.width, tmpl.height)) {
                        int holesOutside = CountHolesExcludingRegion(child.board, tmpl.originX,
                                                                       tmpl.originY, tmpl.width, tmpl.height);
                        if (holesOutside <= baseHolesOutside) {
                            child.success = true;
                        }
                    }

                    nextBeam.push_back(std::move(child));
                }
            }

            // ---- 候補B: ホールドと入れ替えて置く ----
            if (parent.canHold && holdCandidate != PType::COUNT && holdCandidate != curType) {
                auto placements = EnumerateAllPlacements(parent.board, holdCandidate,
                                                          false, PType::I, 0, 0);
                for (auto& p : placements) {
                    if (nodesVisited++ > nodeLimit) goto search_limit_reached;

                    LearnBeamNode child;
                    child.board = p.board;
                    // holdCandidate を置いたので、ホールドには curType が入る。
                    // holdCandidate が「未使用ホールド枠から先出しした次のミノ」だった場合、
                    // そのミノは remaining の先頭 (restAfterCur.front()) なのでさらに1つ消費する。
                    if (parent.holdFilled) {
                        child.remaining = restAfterCur;
                    } else {
                        // restAfterCur の先頭を holdCandidate として使ったので、それを取り除く
                        child.remaining = std::vector<PType>(restAfterCur.begin() + 1, restAfterCur.end());
                    }
                    child.hold = curType;
                    child.holdFilled = true;
                    child.canHold = allowHold;
                    child.slots = parent.slots;
                    child.slots.push_back(TemplateSlot{p.x - tmpl.originX, p.y - tmpl.originY, p.rot});
                    child.used = parent.used;
                    child.used.push_back(holdCandidate);

                    if (RegionFilled(child.board, tmpl.originX, tmpl.originY, tmpl.width, tmpl.height)) {
                        int holesOutside = CountHolesExcludingRegion(child.board, tmpl.originX,
                                                                       tmpl.originY, tmpl.width, tmpl.height);
                        if (holesOutside <= baseHolesOutside) {
                            child.success = true;
                        }
                    }

                    nextBeam.push_back(std::move(child));
                }
            }
        }

        if (nextBeam.empty()) break;

        // 成功ノードがあれば即座に採用する
        for (auto& n : nextBeam) {
            if (n.success) {
                outSlots = n.slots;
                outUsedPieces = n.used;
                return true;
            }
        }

        // ビーム幅に絞る。スコアは「注目領域の充填率」を簡易指標にする。
        auto regionFillScore = [&](const LearnBeamNode& n) {
            int filled = 0;
            for (int y = tmpl.originY; y < tmpl.originY + tmpl.height; ++y) {
                if (y < 0 || y >= static_cast<int>(n.board.size())) continue;
                for (int x = tmpl.originX; x < tmpl.originX + tmpl.width; ++x) {
                    if (x < 0 || x >= BOARD_W) continue;
                    if (n.board[y] & (1u << x)) filled++;
                }
            }
            return filled;
        };

        if (static_cast<int>(nextBeam.size()) > beamWidth) {
            std::partial_sort(nextBeam.begin(), nextBeam.begin() + beamWidth, nextBeam.end(),
                               [&](const LearnBeamNode& a, const LearnBeamNode& b) {
                                   return regionFillScore(a) > regionFillScore(b);
                               });
            nextBeam.resize(beamWidth);
        }

        beam = std::move(nextBeam);
    }

search_limit_reached:
    for (auto& n : beam) {
        if (n.success) {
            outSlots = n.slots;
            outUsedPieces = n.used;
            return true;
        }
    }
    return false;
}

int TemplateLearner::learn(int templateIndex, const TemplateLearnOptions& options) {
    LearnedTemplate& tmpl = templates[templateIndex];

    // 探索対象の順列を決める。
    // 1. まず failedSequences (過去に失敗した順列) を優先的に再検証する。
    // 2. その後、まだ試していない新しい順列を追加で試す。
    std::vector<std::vector<PType>> toTry;
    toTry.reserve(tmpl.failedSequences.size() + options.maxSequencesToTry);

    for (auto& seq : tmpl.failedSequences) toTry.push_back(seq);
    tmpl.failedSequences.clear();

    if (options.useBagPermutations) {
        auto allPerms = GenerateBagPermutations(options.maxSequenceLength);
        size_t limit = std::min(allPerms.size(), options.maxSequencesToTry);
        for (size_t i = 0; i < limit; ++i) {
            toTry.push_back(allPerms[i]);
        }
    }

    int newlySucceeded = 0;

    for (auto& seq : toTry) {
        std::vector<TemplateSlot> slots;
        std::vector<PType> used;
        bool ok = trySequence(tmpl, seq, options.allowHold, options.nodeLimit, slots, used);

        tmpl.totalSequencesTried++;

        if (ok) {
            tmpl.totalSequencesSucceeded++;
            newlySucceeded++;

            if (tmpl.slotOrder.empty()) {
                tmpl.slotOrder = slots;
            }

            for (size_t i = 0; i < slots.size() && i < used.size(); ++i) {
                SlotMinoStats& stats = tmpl.slotStats[slots[i]];
                int pi = static_cast<int>(used[i]);
                if (pi >= 0 && pi < NUM_PTYPES) {
                    stats.successCount[pi]++;
                    stats.attemptCount[pi]++;
                    stats.totalAttempts++;
                }
            }
        } else {
            tmpl.failedSequences.push_back(seq);
        }
    }

    return newlySucceeded;
}

void TemplateLearner::learnAll(const TemplateLearnOptions& options) {
    for (size_t i = 0; i < templates.size(); ++i) {
        learn(static_cast<int>(i), options);
    }
}
