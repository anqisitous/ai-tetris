#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

#include "game_engine.h"

namespace FumenDecoder {

constexpr int FIELD_W = 10;
constexpr int FIELD_H = 24;
constexpr int FIELD_BLOCKS = FIELD_W * FIELD_H;

struct DecodedPage {
    std::array<uint8_t, FIELD_BLOCKS> fieldRaw{};
};

std::vector<DecodedPage> decode(const std::string& fumenInput);
std::bitset<30> pageToTopNRows(const DecodedPage& page, int depth);
BoardBits pageToBoardBits(const DecodedPage& page);

}  // namespace FumenDecoder
