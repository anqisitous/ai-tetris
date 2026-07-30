// ===================================================================
// ai_templates.cpp - Implementazione template
// ===================================================================
#include "ai_templates.h"
#include "ai_evaluate.h"

// ---- Matching di un template attivo ----
bool ActiveTemplate::matches(const BoardBits& board, 
                              const std::deque<PType>& bag) const {
    auto& stage = definition->stages.at(currentStage);
    
    // Estrae le top 3 righe
    auto top3 = GetTop3Rows(board);
    
    // Verifica board corrente
    if (top3 != stage.boards[currentBoardIndex]) return false;
    
    // Verifica condizioni bag
    for (auto& cond : stage.conditions) {
        bool seenBefore = false;
        for (auto& p : bag) {
            if (p == cond.before) seenBefore = true;
            if (p == cond.after && !seenBefore) return false;
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