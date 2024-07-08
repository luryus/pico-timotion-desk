#include "movestatemachine.hh"
#include "log.hh"

#include <cstdlib>
#include "pico/time.h"
#include "deskstate.hh"

#define DESK_CMD_UP 0x02
#define DESK_CMD_DOWN 0x01
#define DESK_CMD_CALIBRATION (DESK_CMD_UP | DESK_CMD_DOWN)
#define DESK_CMD_STOP 0x00

using State = MoveStateMachine::State;
using Dir = MoveStateMachine::Dir;

State MoveStateMachine::state() const
{
    return current_state_;
}

void MoveStateMachine::start_manual_move(Dir dir)
{
    if (current_state_ != State::Idle)
    {
        LOGD("Cannot start a move, because not idle");
        return;
    }

    move_dir_ = dir;
    change_state(State::Initializing);
}

void MoveStateMachine::start_auto_move(uint8_t target_height, uint8_t current_height)
{
    if (current_state_ != State::Idle)
    {
        LOGD("Cannot start a move, because not idle");
        return;
    }

    // Adjust the target because the table does not stop instantly
    // If we move a long height diff, the table will accelerate to full speed
    // and overshoot

    int8_t dh = current_height > target_height ? 1 : -1;
    int8_t abs_diff = current_height > target_height
        ? current_height - target_height
        : target_height - current_height;

    uint8_t adjusted_height = target_height;

    if (abs_diff >= 9) {
        adjusted_height += 3 * dh;
    } else if (abs_diff >= 5) {
        adjusted_height += 2 * dh;
    } else if (abs_diff >= 3) {
        adjusted_height += dh;
    }

    if (target_height != adjusted_height) {
        LOGI("Adjusted target to %d to counter overshoot", adjusted_height);
    }

    target_height_ = adjusted_height;
    change_state(State::Initializing);
}

void MoveStateMachine::start_calibration_move()
{
    auto manual_move_dir = is_manual_moving();
    bool current_state_valid = current_state_ == State::Idle
        || (manual_move_dir && *manual_move_dir != Dir::Calibration);

    if (!current_state_valid) {
        LOGD("Cannot start calibration move because the current state is invalid for that");
        return;
    }

    reset_fields();
    move_dir_ = Dir::Calibration;
    change_state(State::Initializing);
}

std::optional<Dir> MoveStateMachine::is_manual_moving() const
{
    if (current_state_ == State::Moving && !target_height_.has_value()) {
        return move_dir_;
    }

    return std::nullopt;
}

std::optional<uint8_t> MoveStateMachine::target_height() const
{
    return target_height_;
}

std::optional<Dir> MoveStateMachine::is_auto_moving() const
{
    if (current_state_ == State::Moving && target_height_.has_value()) {
        return move_dir_;
    }

    return std::nullopt;
}


uint8_t MoveStateMachine::get_send_msg() const
{
    switch (current_state_)
    {
    case State::Moving:
        // State validation should make sure that move_dir_ has a value set
        switch (move_dir_.value()) {
        case Dir::Up:
            return DESK_CMD_UP;
        case Dir::Calibration:
            return DESK_CMD_CALIBRATION;
        case Dir::Down:
        default:
            return DESK_CMD_DOWN;
        }

    // In all other cases, return stop
    case State::Idle:
    case State::Initializing:
    case State::Stopping:
    case State::Recovering:
        // TODO implement recovery mode
    case State::Error:
    default:
        return DESK_CMD_STOP;
    }
}

void MoveStateMachine::stop()
{
    if (current_state_ == State::Idle || current_state_ == State::Stopping || current_state_ == State::Error) {
        LOGW("Cannot stop from the current state");
        return;
    }
    LOGI("MoveStateMachine: stop requested");
    stopping_started_at_ = get_absolute_time();
    if (!last_height_change_.has_value()) {
        last_height_change_ = nil_time;
    }
    change_state(State::Stopping);
}

bool MoveStateMachine::is_error() const {
    return current_state_ == State::Error;
}

void MoveStateMachine::clear_error() {
    change_state(State::Idle);
}

void MoveStateMachine::update_desk_state(const DeskState &desk_state)
{
    if (desk_state.state_age_ms() > 200) {
        LOGD("MoveStateMachine: Received too old desk state, ignoring");
        return;
    }

    if (current_state_ == State::Initializing)
    {
        auto height = desk_state.height_cm();
        initial_height_ = height;
        previous_height_ = height;
        current_height_ = height;
        last_height_change_ = get_absolute_time();

        if (target_height_.has_value())
        {
            move_dir_ = (height > *target_height_) ? Dir::Down : Dir::Up;
        }
        change_state(State::Moving);
    }

    else if (current_state_ == State::Moving)
    {
        auto height = desk_state.height_cm();
        previous_height_ = current_height_;
        current_height_ = height;
        if (*previous_height_ != *current_height_) {
            last_height_change_= get_absolute_time();
        }
        if (target_reached()) {
            stop();
        }
    }

    else if (current_state_ == State::Stopping)
    {
        auto height = desk_state.height_cm();
        previous_height_ = current_height_;
        current_height_ = height;
        if (*previous_height_ != *current_height_) {
            last_height_change_= get_absolute_time();
        }
        if (time_reached(delayed_by_ms(*stopping_started_at_, 500))
            && time_reached(delayed_by_ms(*last_height_change_, 500)))
        {
            change_state(State::Idle);   
        }
    }

    validate_state();
}

bool MoveStateMachine::target_reached() const
{
    if (!move_dir_.has_value() || !target_height_.has_value() || !current_height_.has_value()) {
        return false;
    }

    if (*move_dir_ == Dir::Up) {
        return *current_height_ >= *target_height_;
    } else {
        return *current_height_ <= *target_height_;
    }
}

bool MoveStateMachine::validate_state()
{
    bool is_error = false;
    switch (current_state_)
    {
    case State::Idle:
        reset_fields();
        break;

    case State::Initializing:
        if (!(move_dir_.has_value() ^ target_height_.has_value()))
        {
            is_error = true;
            LOGE("State::Initializing: either none or both of target height and move dir were set");
        }
        else if (initial_height_.has_value() || current_height_.has_value() || previous_height_.has_value())
        {
            is_error = true;
            LOGE("State::Initializing: initial/current/previous height were set");
        }
        break;

    case State::Moving:
    {
        if (!move_dir_.has_value())
        {
            is_error = true;
            LOGE("State::Moving: dir not set");
            break;
        }
        if (!current_height_.has_value() || !previous_height_.has_value() || !initial_height_.has_value() || !last_height_change_.has_value())
        {
            is_error = true;
            LOGE("State::Moving: current/previous/initial height or last height change ts not set");
            break;
        }
        auto dir = move_dir_.value();
        if (target_height_.has_value())
        {
            if (dir == Dir::Up && *current_height_ > *target_height_)
            {
                is_error = true;
                LOGE("State:Moving: target height passed");
            }
            else if (dir == Dir::Down && *current_height_ < *target_height_)
            {
                is_error = true;
                LOGE("State:Moving: target height passed");
            }

            if ((dir == Dir::Up && *previous_height_ > *target_height_) ||
                (dir == Dir::Down && *previous_height_ < *target_height_))
            {
                is_error = true;
                LOGE("State::Moving: desk moved in wrong direction");
            }
        }

        int move_timeout_ms = dir == Dir::Calibration ? 20'000 : 2'000;
        if (time_reached(delayed_by_ms(*last_height_change_, move_timeout_ms)))
        {
            is_error = true;
            LOGE("State::Moving: desk height not changed in %d ms", move_timeout_ms);
        }

        break;
    }

    case State::Stopping:
        if (!stopping_started_at_.has_value() || !last_height_change_.has_value()) {
            is_error = true;
            LOGE("State::Stopping: Required timestamps were not set");
        }
        break;

    case State::Recovering:
        is_error = true;
        LOGE("State::Recovering: currently not supported");
        break;
    case State::Error:
        break;
    }

    if (is_error)
    {
        change_state(State::Error);
    }
    return is_error;
}

void MoveStateMachine::change_state(State new_state)
{
    if (new_state == current_state_)
    {
        LOGD("change_state called unnecessarily: already in state %s", state_str(new_state));
        return;
    }

    LOGI("Moving to state: %s", state_str(new_state));
    current_state_ = new_state;
    validate_state();
}

void MoveStateMachine::reset_fields()
{
    initial_height_ = std::nullopt;
    target_height_ = std::nullopt;
    current_height_ = std::nullopt;
    previous_height_ = std::nullopt;
    last_height_change_ = std::nullopt;
    stopping_started_at_ = std::nullopt;
    move_dir_ = std::nullopt;
}

const char* MoveStateMachine::state_str(State state) {
    const char *state_str = nullptr;
    switch (state)
    {
    case State::Idle:
        state_str = "Idle";
        break;
    case State::Initializing:
        state_str = "Initializing";
        break;
    case State::Moving:
        state_str = "Moving";
        break;
    case State::Stopping:
        state_str = "Stopping";
        break;
    case State::Recovering:
        state_str = "Recovering";
        break;
    case State::Error:
        state_str = "Error";
        break;
    default:
        state_str = "???";
        break;
    }
    return state_str;
}


const char* MoveStateMachine::dir_str(Dir dir) {
    const char *dir_str = nullptr;
    switch (dir)
    {
    case Dir::Up:
        dir_str = "Up";
        break;
    case Dir::Down:
        dir_str = "Down";
        break;
    case Dir::Calibration:
        dir_str = "Calib";
        break;
    default:
        dir_str = "???";
        break;
    }
    return dir_str;
}