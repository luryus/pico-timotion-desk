#include "deskstate.hh"

#include "log.hh"

uint8_t DeskState::height_cm() const
{
    return height_cm_;
}

bool DeskState::initialized() const
{
    return initialized_;
}

bool DeskState::is_moving() const
{
    return is_moving_;
}

std::optional<uint8_t> DeskState::storing_mem_slot() const
{
    return storing_mem_slot_;
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

void DeskState::update(DeskCmd &cmd)
{
    last_state_received_at_ = get_absolute_time();

    if (auto msg = cmd.get_and_reset())
    {
        initialized_ = true;
        auto status = msg->first;
        auto data = msg->second;

        LOGD("Updating desk state: status %02x, data %02x", status, data);
    }
}