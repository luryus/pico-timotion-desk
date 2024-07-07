#include "deskcmd.hh"

#include "log.hh"


DeskCmd::DeskCmd()
{
    queue_init(&rx_queue_, sizeof(uint8_t), 24);
}

DeskCmd::~DeskCmd()
{
    queue_free(&rx_queue_);
}

bool DeskCmd::receive(uint8_t data)
{
    return queue_try_add(&rx_queue_, &data);
}

void DeskCmd::flush_rx_queue()
{
    uint8_t item;
    while (queue_try_remove(&rx_queue_, &item)) {
        bytes_.try_push_back(item);
    }

    while (!bytes_.empty() && !validate()) {
        bytes_.pop_front();
    }
}

void DeskCmd::reset()
{
    bytes_.clear();
}

std::optional<DeskCmd::Error> DeskCmd::last_error() const
{
    return last_error_;
}

bool DeskCmd::validate()
{
    auto len = bytes_.size();
    if (len >= 1 && bytes_.at(0).value() != 0x98)
    {
        last_error_ = DeskCmd::Error::HeaderBytesInvalid;
        return false;
    }

    if (len >= 2 && bytes_.at(1).value() != 0x98)
    {
        last_error_ = DeskCmd::Error::HeaderBytesInvalid;
        return false;
    }

    if (len >= 4 && bytes_.at(2).value() != bytes_.at(3).value())
    {
        last_error_ = DeskCmd::Error::MiddleBytesDiffer;
        return false;
    }

    if (len >= 6 && bytes_.at(4).value() != bytes_.at(5).value())
    {
        last_error_ = DeskCmd::Error::LastBytesDiffer;
        return false;
    }

    return true;
}

bool DeskCmd::is_ready()
{
    flush_rx_queue();
    return bytes_.size() >= 6;
}

std::optional<std::pair<uint8_t, uint8_t>> DeskCmd::take()
{
    if (!is_ready())
    {
        return std::nullopt;
    }

    auto res = std::pair(bytes_.at(2).value(), bytes_.at(4).value());
    
    // Remove first 6 bytes
    for (auto i = 0; i < 6; i++) {
        bytes_.pop_front();
    }

    last_error_ = std::nullopt;
    return res;
}