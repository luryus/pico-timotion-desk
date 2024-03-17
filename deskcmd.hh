#ifndef _DESKCMD_HH
#define _DESKCMD_HH

#include <array>
#include <optional>
#include <tuple>
#include <cstdint>

class DeskCmd {

public:
    enum class Error { HeaderBytesInvalid, MiddleBytesDiffer, LastBytesDiffer };

    bool update(uint8_t data);
    bool is_ready() const;

    std::optional<Error> last_error() const;
    void reset();

    std::optional<std::pair<uint8_t, uint8_t>> get_and_reset();

private:
    bool validate();
    std::array<uint8_t, 6> bytes_;
    std::uint8_t len_ = 0;
    std::optional<Error> last_error_;
};

#endif