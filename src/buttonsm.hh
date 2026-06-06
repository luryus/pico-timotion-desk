#ifndef _BUTTONSM_HH
#define _BUTTONSM_HH

#include "pico/time.h"



class ButtonSm {

public:
    static inline constexpr uint64_t ShortPressMs = 300;
    static inline constexpr uint64_t DirectionButtonLongPressMs = 600;
    static inline constexpr uint64_t PresetButtonLongPressMs = 3000;

    ButtonSm(uint64_t long_press_limit_ms) : long_press_limit_ms_(long_press_limit_ms) {}
    static ButtonSm direction_button() { return ButtonSm(DirectionButtonLongPressMs); }
    static ButtonSm preset_button() { return ButtonSm(PresetButtonLongPressMs); }

    enum class State : uint8_t {
        Idle, Press1Pending, LongPressStart, LongPress,
        Release1Pending, ShortPress, Press2Pending,
        DoubleLongPressStart, DoubleLongPress, DoublePress,
    };

    enum class ButtonEvent : uint8_t {
        Idle,
        ShortPress, DoublePress,
        LongPressStart, LongPressDown,
        DoubleLongPressStart, DoubleLongPressDown
    };

    void tick(bool is_pressed);
    void force_idle();
    void consume();
    ButtonEvent event() const;

private:
    void next(State state);
    uint64_t time_in_current_state_ms() const;

    uint64_t long_press_limit_ms_;
    absolute_time_t last_state_transition_ = nil_time;
    State state_ = State::Idle;
};

#endif