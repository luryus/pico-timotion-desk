#include "deskcmd.hh"

#include "log.hh"

bool DeskCmd::update(uint8_t data)
{
    if (len_ >= 6)
    {
        return false;
    }

    bytes_[len_++] = data;

    if (!validate())
    {
        reset();
        return false;
    }

    return true;
}

void DeskCmd::reset()
{
    len_ = 0;
}

std::optional<DeskCmd::Error> DeskCmd::last_error() const
{
    return last_error_;
}

bool DeskCmd::validate()
{
    if (len_ >= 1 && bytes_[0] != 0x98)
    {
        last_error_ = DeskCmd::Error::HeaderBytesInvalid;
        return false;
    }

    if (len_ >= 2 && bytes_[1] != 0x98)
    {
        last_error_ = DeskCmd::Error::HeaderBytesInvalid;
        return false;
    }

    if (len_ >= 4 && bytes_[2] != bytes_[3])
    {
        last_error_ = DeskCmd::Error::MiddleBytesDiffer;
        return false;
    }

    if (len_ >= 6 && bytes_[4] != bytes_[5])
    {
        last_error_ = DeskCmd::Error::LastBytesDiffer;
        return false;
    }

    return true;
}

bool DeskCmd::is_ready() const
{
    return len_ == 6;
}

std::optional<std::pair<uint8_t, uint8_t>> DeskCmd::get_and_reset()
{
    if (!is_ready())
    {
        return std::nullopt;
    }

    auto res = std::pair(bytes_[2], bytes_[4]);
    reset();
    last_error_ = std::nullopt;
    return res;
}