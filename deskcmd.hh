#ifndef _DESKCMD_HH
#define _DESKCMD_HH

#include <array>
#include <optional>
#include <tuple>
#include <cstdint>

#include "ringbuffer.hh"
#include "pico/util/queue.h"

class DeskCmd {

public:
    DeskCmd();
    ~DeskCmd();

    enum class Error { HeaderBytesInvalid, MiddleBytesDiffer, LastBytesDiffer };

    bool receive(uint8_t data);
    bool is_ready();

    std::optional<Error> last_error() const;
    void reset();

    std::optional<std::pair<uint8_t, uint8_t>> take();

private:
    bool validate();
    void flush_rx_queue();
    queue_t rx_queue_;
    RingBuffer<uint8_t, 24> bytes_;
    std::optional<Error> last_error_;
};

#endif