// ===================================================================
// ai_templates.h - Template Fissi
// ===================================================================
#pragma once
#include "game_engine.h"
#include <bitset>
#include <vector>
#include <string>
#include <deque>
#include <unordered_map>
#include <memory>

// ---- Condizione ordine nella bag ----
struct BagCondition {
    PType before;
    PType after;
};

// ---- Stage di un template ----
struct StageDefinition {
    int numBoards;
    int searchDepth = 3;  // GetTopNRows/pageToTopNRowsで抽出する上位行数(1〜3)
    std::vector<std::bitset<30>> boards;
    std::vector<BagCondition> conditions;
    std::string nextStage;  // Prossimo stage (vuoto se ultimo)
};

// ---- Template completo ----
struct TemplateDefinition {
    std::string name;
    std::unordered_map<std::string, StageDefinition> stages;
    std::string startStage;
};

// ---- Istanza attiva di un template ----
struct ActiveTemplate {
    const TemplateDefinition* definition;
    std::string currentStage;
    int currentBoardIndex;
    
    std::bitset<30> targetBoard() const {
        auto& stage = definition->stages.at(currentStage);
        return stage.boards[currentBoardIndex];
    }
    
    bool advance() {
        auto& stage = definition->stages.at(currentStage);
        if (currentBoardIndex < stage.boards.size() - 1) {
            currentBoardIndex++;
            return true;
        }
        if (!stage.nextStage.empty()) {
            currentStage = stage.nextStage;
            currentBoardIndex = 0;
            return true;
        }
        return false;
    }
    
    bool matches(const BoardBits& board, const std::vector<PType>& bag) const;
};

// ---- BoardBitsの最上段からdepth行(1〜3)を抽出しbitsetへ詰める ----
// localRow 0が最上段(fumen_decoder.h::pageToTopNRowsと同一の規約)。
std::bitset<30> GetTopNRows(const BoardBits& board, int depth);

// ---- Libreria template ----
class TemplateLibrary {
private:
    std::vector<TemplateDefinition> templates;
    
public:
    void addTemplate(const TemplateDefinition& tmpl) {
        templates.push_back(tmpl);
    }
    
    std::vector<ActiveTemplate> match(const BoardBits& board, 
                                       const std::vector<PType>& bag) const;
    
    const std::vector<TemplateDefinition>& getAll() const { return templates; }
};

// ---- TemplateLoader - tmpl / fumenリスト / バイナリキャッシュ の読み書き ----
// 実装は ai_templates.cpp。
// 元々このリポジトリには宣言(プロトタイプ)が存在せず、main_sdl3.cppと
// ai_server.cppの両方が定義のみに依存していたため、ai_templates.cppと
// 同一の翻訳単位でコンパイルされる場合を除きリンクエラーになっていた
// (今回のoracle対応の一環として、この既存の不備を合わせて修正した)。
namespace TemplateLoader {
int loadTmplFile(const std::string& path, TemplateLibrary& outLib);
int loadFumenListFile(const std::string& path, TemplateLibrary& outLib, int searchDepth);
bool buildTerrainCache(const std::string& fumenListPath, const std::string& outCachePath,
                        int searchDepth);
int loadTerrainCache(const std::string& cachePath, TemplateLibrary& outLib);
}  // namespace TemplateLoader