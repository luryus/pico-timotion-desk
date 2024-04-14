#include "buttonsm.hh"

using State = ButtonSm::State;
using ButtonEvent = ButtonSm::ButtonEvent;

void ButtonSm::tick(bool is_pressed)
{
    switch (state_)
    {

    case State::Idle:
        if (is_pressed)
        {
            next(State::Press1Pending);
        }
        break;
    case State::Press1Pending:
        if (!is_pressed)
        {
            next(State::Release1Pending);
        }
        else if (time_in_current_state_ms() > 500)
        {
            next(State::LongPress);
        }
        break;
    case State::Release1Pending:
        if (is_pressed)
        {
            next(State::Press2Pending);
        }
        else if (time_in_current_state_ms() > 300)
        {
            next(State::ShortPress);
        }
        break;
    case State::Press2Pending:
        if (!is_pressed)
        {
            next(State::DoublePress);
        }
        else if (time_in_current_state_ms() > 500)
        {
            next(State::DoubleLongPress);
        }
        break;
    case State::LongPress:
    case State::DoubleLongPress:
        if (!is_pressed)
        {
            next(State::Idle);
        }
        break;
    case State::ShortPress:
    case State::DoublePress:
        next(State::Idle);
        break;
    }
}

ButtonEvent ButtonSm::event() const
{
    switch (state_)
    {
    case State::ShortPress:
        return ButtonEvent::ShortPress;
    case State::DoublePress:
        return ButtonEvent::DoublePress;
    case State::LongPress:
        return ButtonEvent::LongPressDown;
    case State::DoubleLongPress:
        return ButtonEvent::DoubleLongPressDown;
    case State::Idle:
    case State::Press1Pending:
    case State::Press2Pending:
    case State::Release1Pending:
    default:
        return ButtonEvent::Idle;
    }
}

void ButtonSm::next(State state)
{
    state_ = state;
    last_state_transition_ = get_absolute_time();
}

uint64_t ButtonSm::time_in_current_state_ms() const
{
    return absolute_time_diff_us(last_state_transition_, get_absolute_time()) / 1000;
}
