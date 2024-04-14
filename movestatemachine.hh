#ifndef _MOVESTATEMACHINE_HH
#define _MOVESTATEMACHINE_HH

#include <optional>
#include <cstdint>
#include "pico/time.h"

class DeskState;

class MoveStateMachine {
public:
    enum class State : uint8_t {
        Idle,
        Initializing,
        Moving,
        Stopping,
        Recovering,
        Error,
    };

    enum class Dir : uint8_t {
        Up, Down
    };

    State state() const;

    void start_auto_move(uint8_t target_height);
    void start_manual_move(Dir dir);
    void stop();
    bool is_error() const;
    void clear_error();

    std::optional<Dir> is_auto_moving() const;
    std::optional<Dir> is_manual_moving() const;

    void update_desk_state(const DeskState& desk_state);

    uint8_t get_send_msg() const;

    static const char* dir_str(Dir dir);
    static const char* state_str(State state);

private:
    bool validate_state();
    void reset_fields();
    void change_state(State new_state);
    bool target_reached() const;

    State current_state_ = State::Idle;

    std::optional<uint8_t> initial_height_ = std::nullopt;
    std::optional<uint8_t> target_height_ = std::nullopt;
    
    std::optional<uint8_t> previous_height_ = std::nullopt;
    std::optional<uint8_t> current_height_ = std::nullopt;
    std::optional<absolute_time_t> last_height_change_ = std::nullopt;

    std::optional<absolute_time_t> stopping_started_at_ = std::nullopt;

    std::optional<Dir> move_dir_ = std::nullopt;
};

#endif