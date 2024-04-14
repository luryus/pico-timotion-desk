#ifndef _MEMSLOTS_HH
#define _MEMSLOTS_HH

#include <optional>
#include <array>
#include <cstdint>


const uint8_t NUM_SLOTS = 4;

bool memslots_validate();
std::array<std::optional<uint8_t>, NUM_SLOTS> memslots_get_all();
void memslots_set(uint8_t slot, uint8_t height);
std::optional<uint8_t> memslots_get(uint8_t slot);
void memslots_reset();

#endif
