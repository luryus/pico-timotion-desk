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
    class Notification {
    public:
        enum class Type {
            Stored
        };

        Notification(Type type, uint32_t param = 0) : type_(type), param_(param) {}

        bool advance() { return --frames_remaining_ > 0; }
        Type type() const { return type_; };
        uint32_t param() const { return param_; }
    private:
        Type type_;
        uint32_t param_;
        uint8_t frames_remaining_ = 50;
    };

    struct DisplayState {
        DeskState desk_state;
        MoveStateMachine::State move_state;
        std::optional<MoveStateMachine::Dir> move_dir;
        std::optional<uint8_t> target_height;
        bool waking_desk;
        std::optional<Notification> timed_notification;
    };


    Display(uint8_t sda = 16, uint8_t scl = 17, i2c_inst_t* i2c = i2c0);
    void init();
    void post_display_state(DisplayState state);

    void run();
    DisplayState const& current_display_state() const;

private:
    void draw();
    void set_sleep_state(bool sleep_state);
    void init_i2c() const;
    void draw_up_anim();
    void draw_down_anim();
    void draw_anim(int8_t anim_dir, const uint8_t* bitmap);
    static uint8_t pico_u8g2_byte_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
    static uint8_t pico_u8g2_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, __attribute__((unused)) void *arg_ptr);

    DisplayState state_;
    mutex mutex_;
    u8g2_t u8g2_;
    bool in_sleep_;

    uint8_t sda_;
    uint8_t scl_;
    i2c_inst_t* i2c_;

    bool initialized_ = false;
};

#endif