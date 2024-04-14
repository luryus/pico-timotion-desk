#ifndef _BUTTONSM_HH
#define _BUTTONSM_HH

#include "pico/time.h"

class ButtonSm {

public:
    enum class State : uint8_t {
        Idle, Press1Pending, LongPress, Release1Pending,
        ShortPress, Press2Pending, DoubleLongPress, DoublePress,
    };

    enum class ButtonEvent : uint8_t {
        Idle, ShortPress, DoublePress, LongPressDown, DoubleLongPressDown
    };

    void tick(bool is_pressed);
    ButtonEvent event() const;

private:
    void next(State state);
    uint64_t time_in_current_state_ms() const;

    absolute_time_t last_state_transition_ = nil_time;
    State state_ = State::Idle;
};

#endif