#ifndef _RINGBUFFER_HH
#define _RINGBUFFER_HH

#include <optional>
#include <memory>
#include <functional>
#include <cstdint>

template <typename T, std::size_t N>
class RingBuffer {

public:
    RingBuffer() : buffer_(new std::optional<T>[N]) {}

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) noexcept = default;
    RingBuffer& operator=(RingBuffer&&) noexcept = default;

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return N; }
    bool full() const { return size_ >= N; }
    bool empty() const { return size_ == 0; }
    void clear()
    {
        while (size_ > 0) {
            pop_back();
        }
        start_ = 0;
    }

    std::optional<std::reference_wrapper<const T>> at(std::size_t pos) const
    {
        if (pos < size_) {
            const auto ref = std::reference_wrapper(*buffer_[buffer_idx(pos)]);
            return std::make_optional(ref);
        }

        return std::nullopt;
    }

    bool try_push_back(T const& elem)
    {
        if (full()) {
            return false;
        }

        buffer_[buffer_idx(size_)] = elem;
        size_++;
        return true;
    }

    bool try_push_back(T&& elem)
    {
        if (full()) {
            return false;
        }
        buffer_[buffer_idx(size_)].emplace(std::move(elem));
        size_++;
        return true;
    }

    bool pop_back()
    {
        if (empty()) {
            return false;
        }

        buffer_[buffer_idx(size_ - 1)].reset();
        size_--;
        return true;
    }

    bool pop_front()
    {
        if (empty()) {
            return false;
        }
        buffer_[buffer_idx(0)].reset();
        start_ = (start_ + 1) % N;
        size_--;
        return true;
    }

private:
    std::size_t buffer_idx(std::size_t pos) const { return (pos + start_) % N; }

    std::unique_ptr<std::optional<T>[]> buffer_;
    std::size_t size_ = 0;
    std::size_t start_ = 0;
};

#endif