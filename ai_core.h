// ===================================================================
// ai_core.h - AI Decision Making
// ===================================================================
#pragma once
#include "game_engine.h"
#include "ai_evaluate.h"
#include "ai_templates.h"
#include <vector>
#include <deque>
#include <memory>

// ---- Azione dell'AI ----
struct AIAction {
    int targetX = 0;
    int targetRot = 0;
    bool shouldHold = false;
    bool shouldDrop = false;
    bool ready = false;
    bool holdDone = false;
    
    // Per apprendimento
    int predictedDamage = 0;
    Aspect predictedOppBoard;
    Aspect beforeBoard;
    bool hasPrediction = false;
};

// ---- Memoria pattern dinamici (max 70) ----
struct PatternNeuron {
    Aspect targetAspect;
    std::vector<PType> bagSequence;
    int depth;
    int confidence;
    uint64_t lshHash;
};

class PatternMemory {
private:
    std::vector<PatternNeuron> patterns;  // Max 70
    
public:
    void addPattern(const PatternNeuron& p);
    PatternNeuron* findBestMatch(const Aspect& current);
    size_t size() const { return patterns.size(); }
};

// ---- Stato dell'AI ----
struct AIState {
    PatternMemory patternMemory;
    ActiveTemplate* activeTemplate = nullptr;
    TemplateLibrary* templateLib = nullptr;
    
    double dasDelay = 0.10;
    double arrDelay = 0.02;
    double thinkInterval = 0.10;
    double thinkTimer = 0.0;
};

// ---- Funzioni AI ----
AIAction decideAI(AIState& state, PlayerState& self, PlayerState& opp);
void executeAI(PlayerState& ps, AIAction& act, double dt, double das, double arr);
AIAction makeAction(const PlacementResult& best, bool usedHold);

// ---- Apprendimento ----
void learnFromPlacement(AIState& state, const Aspect& before, 
                         const Aspect& after, float reward);