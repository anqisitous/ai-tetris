// ===================================================================
// ai_evaluate.h - Valutazione del terreno
// ===================================================================
#pragma once
#include "game_engine.h"
#include <bitset>
#include <vector>

// ---- Aspetto del terreno (feature per AI) ----
struct Aspect {
    std::vector<float> values;
    BoardBits snapshot;
};

// ---- Funzioni di valutazione ----
Aspect extractAspect(const BoardBits& board, float timingDiff = 0.0f,
                     int combo = 0, const std::deque<PType>& next = {});

float aspectDistance(const Aspect& a, const Aspect& b);

// ---- Altezze e gruppi ----
int getColumnHeight(const BoardBits& board, int col);
float getGroupHeight(const BoardBits& board, int groupIdx);
std::array<int, 5> getGroupMinHeights(const BoardBits& board);

// ---- Rilevazione TSD / Double Dagger ----
bool isFilled(const BoardBits& board, int col, int row);
bool isTSpin(const BoardBits& board, int x, int y, int rot);
int countTSDDoubleSetups(const BoardBits& board);
bool isDoubleDaggerRight(const BoardBits& board);
bool isDoubleDaggerLeft(const BoardBits& board);
bool isDoubleDagger(const BoardBits& board);

// ---- Qualità del terreno ----
int countHoles(const BoardBits& board);
float evaluateTerrainQuality(const BoardBits& board);
float evaluateDoubleDaggerReadiness(const BoardBits& board);

// ---- Parità e spettro ----
float calculateParitySpectrum(const BoardBits& board);
bool isCenterOpen(const BoardBits& board);

// ---- Utilità ----
std::bitset<30> GetTop3Rows(const BoardBits& board);
std::bitset<30> GetRows(const BoardBits& board, int startRow, int endRow);
uint64_t lshHash(const std::vector<float>& vec, int bits = 20);

// ---- Danno e attacco ----
int calculateDamage(int linesCleared, bool tSpin, bool btb, int combo, bool perfectClear);