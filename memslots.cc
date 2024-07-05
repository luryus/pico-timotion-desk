#include "memslots.hh"

#include <string>
#include <cstring>
#include "persistent_flash.hh"
#include "log.hh"

#include "hardware/flash.h"
#include "pico/flash.h"

#define SLOT(num) (PERSISTENT_FLASH_START + 8 + (num * 4))


static const uint8_t MIN_STORED_HEIGHT = 0x40; 
static const uint8_t MAX_STORED_HEIGHT = 0x80;
static const uint8_t EMPTY_STORED_HEIGHT = 0x00;


static const char HEADER[8] = "DESK001";

bool memslots_validate() {
    // Validate header
    auto header = std::string_view(reinterpret_cast<char*>(PERSISTENT_FLASH_START), 7);
    if (header != std::string_view(HEADER)) {
        LOGW("Persistent flash header invalid");
        return false;
    }

    for (int i = 0; i < NUM_SLOTS; i++) {
        uint8_t* slot = SLOT(i);
        uint8_t height = *slot;
        if (height != EMPTY_STORED_HEIGHT && (height < MIN_STORED_HEIGHT || height > MAX_STORED_HEIGHT))  {
            LOGW("Slot %d data invalid: %02x", i, height);
            return false;
        }
    }

    return true;
}

static void write_flash_core(void* param) {
    // Erase our sector, because that's how flash works
    // If the flash will be used for any other purposes later, this needs to be refactored
    flash_range_erase(PERSISTENT_FLASH_OFFSET, 1 * FLASH_SECTOR_SIZE);
    // Then write the data (only the first page for now)
    flash_range_program(PERSISTENT_FLASH_OFFSET, (const uint8_t*) param, 1 * FLASH_PAGE_SIZE);
}

static void write_flash(std::array<std::optional<uint8_t>, 4> heights)
{
    std::array<uint8_t, FLASH_PAGE_SIZE> arr{};
    std::memcpy(arr.data(), HEADER, 8);
    arr[8 + 0] = heights[0].value_or(EMPTY_STORED_HEIGHT);
    arr[8 + 4] = heights[1].value_or(EMPTY_STORED_HEIGHT);
    arr[8 + 8] = heights[2].value_or(EMPTY_STORED_HEIGHT);
    arr[8 + 12] = heights[3].value_or(EMPTY_STORED_HEIGHT);

    int res = flash_safe_execute(write_flash_core, arr.data(), 1000);
    if (res != PICO_OK) {
        LOGW("Writing flash failed: %d", res);
    }
}

std::optional<uint8_t> memslots_get(uint8_t slot) {
    if (slot >= NUM_SLOTS) {
        return std::nullopt;
    }

    auto h = *SLOT(slot);
    return h == EMPTY_STORED_HEIGHT ? std::nullopt : std::make_optional(h);
}

void memslots_reset()
{
    LOGD("Resetting persistent flash...");
    write_flash({});
    LOGD("Flash reset.");
}

std::array<std::optional<uint8_t>, NUM_SLOTS> memslots_get_all()
{
    if (!memslots_validate()) {
        return {};
    }

    return { memslots_get(0), memslots_get(1), memslots_get(2), memslots_get(3) };
}

void memslots_set(uint8_t slot, uint8_t height)
{
    if (height != EMPTY_STORED_HEIGHT && (height < MIN_STORED_HEIGHT || height > MAX_STORED_HEIGHT)) {
        LOGE("Trying to store height that is too low or high. Ignoring!");
        return;
    }

    if (!memslots_validate()) {
        memslots_reset();
    }

    auto data = memslots_get_all();
    data[slot] = height;
    write_flash(data);
}

void memslots_log() {
    if (!memslots_validate()) {
        LOGW("Memslots invalid");
        return;
    }

    auto memslots_data = memslots_get_all();
    LOGI("Stored mem slots: [0]: %d, [1]: %d, [2]: %d, [3]: %d",
        memslots_data[0].value_or(0), memslots_data[1].value_or(0),
        memslots_data[2].value_or(0), memslots_data[3].value_or(0));
}