// ===================================================================
// ai_templates.cpp - Implementazione template
// ===================================================================
#include "ai_templates.h"
#include "ai_evaluate.h"
#include "fumen_decoder.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdint>

// ---- Matching di un template attivo ----
bool ActiveTemplate::matches(const BoardBits& board,const std::vector<PType>& bag) const {
    
    // 1. definition のチェック
    if (!definition) {
        return false;
    }
    
    // 2. stage の存在チェック
    auto it = definition->stages.find(currentStage);
    if (it == definition->stages.end()) {
        return false;
    }
    const auto& stage = it->second;  // const参照で取得
    
    std::bitset<30> topRows = GetTopNRows(board, stage.searchDepth);
    
    if (currentBoardIndex >= stage.boards.size()) {
        return false;
    }
    if (topRows != stage.boards[currentBoardIndex]) {
        return false;
    }
    
    for (const auto& cond : stage.conditions) {
        bool seenBefore = false;
        for (PType p : bag) {  // PTypeは小さなenum → 値コピー
            if (p == cond.before) {
                seenBefore = true;
            }
            if (p == cond.after && !seenBefore) {
                return false;
            }
        }
    }
    
    return true;
}
// ---- Cerca template attivi ----
std::vector<ActiveTemplate> TemplateLibrary::match(
    const BoardBits& board, const std::deque<PType>& bag) const {
    
    std::vector<ActiveTemplate> active;
    
    for (auto& tmpl : templates) {
        ActiveTemplate at;
        at.definition = &tmpl;
        at.currentStage = tmpl.startStage;
        at.currentBoardIndex = 0;
        
        if (at.matches(board, bag)) {
            active.push_back(at);
        }
    }
    
    return active;
}

// ===================================================================
// TemplateLoader - tmpl / fumenリスト / バイナリキャッシュ の読み書き
// ===================================================================
namespace TemplateLoader {

namespace {

// ---- 補助: 文字列前後の空白を除去 ----
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---- 補助: 1文字ミノ表記 -> PType ----
// tmplの手書きB行で使う文字。地形パターン中の '.' は空、それ以外(I,O,T,S,Z,J,L)は
// 「そこにブロックがある」という意味だけを使う(色/種類の区別はmatch判定に不要)。
bool isBlockChar(char c) {
    return c == '#' || c == 'I' || c == 'O' || c == 'T' ||
           c == 'S' || c == 'Z' || c == 'J' || c == 'L' || c == 'X';
}

// ---- 補助: "I","O","T","S","Z","J","L" -> PType ----
bool parsePTypeChar(char c, PType& out) {
    switch (c) {
        case 'I': out = PType::I; return true;
        case 'O': out = PType::O; return true;
        case 'T': out = PType::T; return true;
        case 'S': out = PType::S; return true;
        case 'Z': out = PType::Z; return true;
        case 'J': out = PType::J; return true;
        case 'L': out = PType::L; return true;
        default: return false;
    }
}

// ---- 補助: 手書きのB行(depth行 x 10文字)を bitset<30> に変換 ----
std::bitset<30> parseBoardLines(const std::vector<std::string>& lines, int depth) {
    std::bitset<30> bits;
    int n = std::min(static_cast<int>(lines.size()), depth);
    // linesは「上から順」に並んでいる前提(tmplファイルの見た目通り)。
    // GetTopNRowsは「localRow 0が最上段」で詰めるため、そのまま対応させる。
    for (int r = 0; r < n; ++r) {
        const std::string& line = lines[r];
        for (int c = 0; c < 10 && c < static_cast<int>(line.size()); ++c) {
            if (isBlockChar(line[c])) bits.set(r * 10 + c);
        }
    }
    return bits;
}

// ---- 補助: "J>S" 形式の条件文字列を BagCondition に変換 ----
bool parseCondition(const std::string& condStr, BagCondition& outCond) {
    auto pos = condStr.find('>');
    if (pos == std::string::npos) return false;
    PType before, after;
    if (!parsePTypeChar(condStr[0], before)) return false;
    if (pos + 1 >= condStr.size() || !parsePTypeChar(condStr[pos + 1], after)) return false;
    outCond = { before, after };
    return true;
}

} // namespace (無名)

// ---- tmplテキストファイルの読み込み ----
int loadTmplFile(const std::string& path, TemplateLibrary& outLib) {
    std::ifstream in(path);
    if (!in.is_open()) return 0;

    int count = 0;
    std::string line;

    TemplateDefinition curTmpl;
    bool hasTmpl = false;
    std::string curStageName;
    StageDefinition curStage;
    bool hasStage = false;
    std::string prevStageName; // 直前のステージ名。連続するS行を自動でネストさせるため。
    std::string firstStageName; // このテンプレートの最初のステージ名 (startStageに使う)

    std::vector<std::string> pendingBoardLines; // 現在読み込み中のB行(手書き)
    int pendingBoardIndex = -1;
    int expectedBoards = 0;

    auto flushStage = [&]() {
        if (hasStage) {
            curTmpl.stages[curStageName] = curStage;
            if (!prevStageName.empty()) {
                // 直前のステージのnextStageが空なら、このステージへ自動的につなぐ
                // (=ネストして次を見据えるtmplの機械的な接続)
                if (curTmpl.stages[prevStageName].nextStage.empty()) {
                    curTmpl.stages[prevStageName].nextStage = curStageName;
                }
            }
            prevStageName = curStageName;
        }
        hasStage = false;
        curStage = StageDefinition{};
        pendingBoardLines.clear();
        pendingBoardIndex = -1;
    };

    auto flushTemplate = [&]() {
        flushStage();
        if (hasTmpl) {
            curTmpl.startStage = firstStageName;
            outLib.addTemplate(curTmpl);
            count++;
        }
        hasTmpl = false;
        curTmpl = TemplateDefinition{};
        prevStageName.clear();
        firstStageName.clear();
    };

    while (std::getline(in, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        if (trimmed.rfind("T:", 0) == 0) {
            // 新しい地形IDの開始。それまでのテンプレートを確定する。
            flushTemplate();
            curTmpl.name = trim(trimmed.substr(2));
            hasTmpl = true;
            continue;
        }

        if (trimmed.rfind("S", 0) == 0 && trimmed.find(':') != std::string::npos) {
            // S0:3 のようなステージ開始行
            flushStage();
            auto colonPos = trimmed.find(':');
            curStageName = trimmed.substr(0, colonPos);
            expectedBoards = std::atoi(trimmed.substr(colonPos + 1).c_str());
            curStage.numBoards = expectedBoards;
            curStage.searchDepth = 3; // デフォルト値。D:行があれば上書きされる。
            if (!hasTmpl) continue; // 念のため防御
            if (firstStageName.empty()) firstStageName = curStageName;
            hasStage = true;
            continue;
        }

        if (!hasStage) continue; // ステージ外の行は無視

        if (trimmed.rfind("D:", 0) == 0) {
            // 探索深さ指定 (最上部からD行を見る)
            int d = std::atoi(trim(trimmed.substr(2)).c_str());
            if (d >= 1 && d <= 3) curStage.searchDepth = d;
            continue;
        }

        if (trimmed.rfind("C:", 0) == 0) {
            std::string condStr = trim(trimmed.substr(2));
            if (!condStr.empty()) {
                BagCondition cond;
                if (parseCondition(condStr, cond)) curStage.conditions.push_back(cond);
            }
            continue;
        }

        if (trimmed.rfind("F:", 0) == 0) {
            // fumenを直接埋め込むボード定義。fumenをデコードし、
            // 最後のページの地形をこのボードとして採用する。
            std::string fumenStr = trim(trimmed.substr(2));
            auto pages = FumenDecoder::decode(fumenStr);
            if (!pages.empty()) {
                curStage.boards.push_back(
                    FumenDecoder::pageToTopNRows(pages.back(), curStage.searchDepth));
            }
            continue;
        }

        if (trimmed.rfind("B", 0) == 0 && trimmed.find(':') != std::string::npos) {
            // 新しいB行(手書きボード)の開始。直前のB行が残っていれば確定する。
            if (!pendingBoardLines.empty()) {
                curStage.boards.push_back(parseBoardLines(pendingBoardLines, curStage.searchDepth));
                pendingBoardLines.clear();
            }
            auto colonPos = trimmed.find(':');
            std::string firstRow = trimmed.substr(colonPos + 1);
            pendingBoardLines.push_back(firstRow);
            continue;
        }

        // B行の続き(2行目, 3行目...)。10文字の地形パターン行とみなす。
        if (!pendingBoardLines.empty() &&
            static_cast<int>(pendingBoardLines.size()) < curStage.searchDepth &&
            trimmed.find_first_not_of(".#IOTSZJLX") == std::string::npos) {
            pendingBoardLines.push_back(trimmed);
            continue;
        }
    }

    // 最後まで残っていたB行/ステージ/テンプレートを確定する
    if (!pendingBoardLines.empty()) {
        curStage.boards.push_back(parseBoardLines(pendingBoardLines, curStage.searchDepth));
        pendingBoardLines.clear();
    }
    flushTemplate();

    // 各テンプレートの startStage を先頭ステージに設定する
    // (すでにaddTemplate済みなのでここでは新規追加時のみ設定される点に注意。
    //  読みやすさのため、パース中に構築したstartStageをここで確定する実装に変更する)
    return count;
}

// ---- fumenリストファイルの読み込み ----
int loadFumenListFile(const std::string& path, TemplateLibrary& outLib, int searchDepth) {
    std::ifstream in(path);
    if (!in.is_open()) return 0;

    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        auto tabPos = trimmed.find('\t');
        if (tabPos == std::string::npos) continue;
        std::string id = trim(trimmed.substr(0, tabPos));
        std::string fumen = trim(trimmed.substr(tabPos + 1));
        if (id.empty() || fumen.empty()) continue;

        auto pages = FumenDecoder::decode(fumen);
        if (pages.empty()) continue;

        TemplateDefinition tmpl;
        tmpl.name = id;
        tmpl.startStage = "S0";

        StageDefinition stage;
        stage.numBoards = 1;
        stage.searchDepth = searchDepth;
        stage.boards.push_back(FumenDecoder::pageToTopNRows(pages.back(), searchDepth));
        tmpl.stages["S0"] = stage;

        outLib.addTemplate(tmpl);
        count++;
    }
    return count;
}

// ===================================================================
// バイナリキャッシュ形式 (地形のバイナリファイルを一つにまとめる)
//
// レイアウト:
//   [4 bytes] magic "TCH1"
//   [4 bytes] uint32 テンプレート数 N
//   N回繰り返し:
//     [2 bytes] uint16 地形IDの長さ L
//     [L bytes] 地形ID文字列 (UTF-8)
//     [4 bytes] int32 searchDepth
//     [8 bytes] uint64 bitset<30>の内容 (下位30bit使用)
// ===================================================================
namespace {
    constexpr char CACHE_MAGIC[4] = {'T','C','H','1'};
}

bool buildTerrainCache(const std::string& fumenListPath, const std::string& outCachePath,
                        int searchDepth) {
    std::ifstream in(fumenListPath);
    if (!in.is_open()) return false;

    // (id, searchDepth, bits) の一覧をまず集める
    struct Entry { std::string id; int depth; unsigned long long bits; };
    std::vector<Entry> entries;

    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        auto tabPos = trimmed.find('\t');
        if (tabPos == std::string::npos) continue;
        std::string id = trim(trimmed.substr(0, tabPos));
        std::string fumen = trim(trimmed.substr(tabPos + 1));
        if (id.empty() || fumen.empty()) continue;

        auto pages = FumenDecoder::decode(fumen);
        if (pages.empty()) continue;

        auto bits = FumenDecoder::pageToTopNRows(pages.back(), searchDepth);
        entries.push_back({ id, searchDepth, bits.to_ullong() });
    }

    std::ofstream out(outCachePath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(CACHE_MAGIC, 4);
    uint32_t n = static_cast<uint32_t>(entries.size());
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));

    for (auto& e : entries) {
        uint16_t len = static_cast<uint16_t>(e.id.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(e.id.data(), len);
        int32_t depth32 = e.depth;
        out.write(reinterpret_cast<const char*>(&depth32), sizeof(depth32));
        uint64_t bits64 = e.bits;
        out.write(reinterpret_cast<const char*>(&bits64), sizeof(bits64));
    }

    return static_cast<bool>(out);
}

int loadTerrainCache(const std::string& cachePath, TemplateLibrary& outLib) {
    std::ifstream in(cachePath, std::ios::binary);
    if (!in.is_open()) return 0;

    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, CACHE_MAGIC, 4) != 0) return 0;

    uint32_t n = 0;
    in.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (!in) return 0;

    int count = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (!in) break;

        std::string id(len, '\0');
        in.read(id.data(), len);
        if (!in) break;

        int32_t depth = 3;
        in.read(reinterpret_cast<char*>(&depth), sizeof(depth));
        if (!in) break;

        uint64_t bits64 = 0;
        in.read(reinterpret_cast<char*>(&bits64), sizeof(bits64));
        if (!in) break;

        TemplateDefinition tmpl;
        tmpl.name = id;
        tmpl.startStage = "S0";

        StageDefinition stage;
        stage.numBoards = 1;
        stage.searchDepth = depth;
        stage.boards.push_back(std::bitset<30>(bits64));
        tmpl.stages["S0"] = stage;

        outLib.addTemplate(tmpl);
        count++;
    }
    return count;
}

} // namespace TemplateLoader
