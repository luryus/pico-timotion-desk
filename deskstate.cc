#include "deskstate.hh"

#include "log.hh"

#define DESK_STATE_FLAG_DISPLAY_ON 0x01
#define DESK_STATE_FLAG_STORE_ON_INV 0x02
#define DESK_STATE_FLAG_STORE_FINISH 0x04

uint8_t DeskState::height_cm() const
{
    return height_cm_;
}

bool DeskState::initialized() const
{
    return initialized_;
}

bool DeskState::is_store_completed() const
{
    return initialized_ && (status_ & DESK_STATE_FLAG_STORE_FINISH);
}

bool DeskState::is_waiting_store() const
{
    // When the desk is waiting store, the second bit "flashes"
    // Normally it's high, but in this case it goes low for a bit, then high, then low again...
    // This probably controls some LCD segments in the original controller?
    return initialized_ && (status_ & DESK_STATE_FLAG_STORE_ON_INV) == 0;
}

bool DeskState::is_display_on() const
{
    return initialized_ && (status_ & DESK_STATE_FLAG_DISPLAY_ON);
}

bool DeskState::is_awake() const
{
    if (!initialized_)
    {
        return false;
    }
    auto diff_us = absolute_time_diff_us(last_state_received_at_, get_absolute_time());
    return diff_us <= 2 * 1'000'000; // 2 seconds
}

uint32_t DeskState::state_age_ms() const
{
    if (!initialized_) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(absolute_time_diff_us(last_state_received_at_, get_absolute_time()) / 1000);
}

void DeskState::update(DeskCmd &cmd)
{
    last_state_received_at_ = get_absolute_time();

    if (auto msg = cmd.get_and_reset())
    {
        initialized_ = true;
        auto status = msg->first;
        auto data = msg->second;

        if (status != status_ || data != height_cm_)
        {
            LOGD("Updating desk state: status %02x, data %02x", status, data);
        }
        status_ = status;
        height_cm_ = data;

    }
}