#ifndef _DISP_HH
#define _DISP_HH

#include <optional>

#include "hardware/i2c.h"
#include "pico/sync.h"
#include "u8g2.h"

#include "deskstate.hh"
#include "movestatemachine.hh"

class Display
{

public:
    struct DisplayState {
        DeskState desk_state;
        MoveStateMachine::State move_state;
        std::optional<MoveStateMachine::Dir> move_dir;
        bool waking_desk;
    };

    Display(uint8_t sda = 12, uint8_t scl = 13, i2c_inst_t* i2c = i2c0);
    void post_display_state(DisplayState state);

    [[noreturn]] void run();

private:
    void draw();
    void set_sleep_state(bool sleep_state);
    void init_i2c() const;
    static uint8_t pico_u8g2_byte_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
    static uint8_t pico_u8g2_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, __attribute__((unused)) void *arg_ptr);

    DisplayState state_;
    mutex mutex_;
    u8g2_t u8g2_;
    bool in_sleep_;

    uint8_t sda_;
    uint8_t scl_;
    i2c_inst_t* i2c_;
};

#endif