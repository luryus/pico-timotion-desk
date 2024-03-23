#ifndef _DESKSTATE_HH
#define _DESKSTATE_HH

#include <stdint.h>
#include <optional>
#include "pico/time.h"
#include "deskcmd.hh"

class DeskState {

public:
    void update(DeskCmd& cmd);

    uint8_t height_cm() const;
    bool initialized() const;
    bool is_waiting_store() const;
    bool is_store_completed() const;
    bool is_display_on() const;
    bool is_awake() const;
    uint32_t state_age_ms() const;

private:
    bool initialized_ = false;

    uint8_t status_ = 0;
    uint8_t height_cm_ = 0;

    absolute_time_t last_state_received_at_ = nil_time;
};

#endif