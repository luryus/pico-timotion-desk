#ifndef _RINGBUFFER_HH
#define _RINGBUFFER_HH

#include <optional>
#include <memory>
#include <functional>
#include <cstdint>

template <typename T, std::size_t N>
class RingBuffer {

public:
    RingBuffer() : buffer_(new T[N]) {}

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return N; }
    bool full() const { return size_ >= N; }
    bool empty() const { return size_ == 0; }
    void clear()
    {
        for (auto i = size_ - 1; i >= 0; i--) {
            std::destroy_at(&buffer_[buffer_idx(i)]);
        }
        
        size_ = 0;
        start_ = 0;

    }

    std::optional<std::reference_wrapper<const T>> at(std::size_t pos) const
    {
        if (pos < size_) {
            return std::make_optional(std::reference_wrapper(buffer_[buffer_idx(pos)]));
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
        buffer_[buffer_idx(size_)] = std::move(elem);
        size_++;
        return true;
    }

    bool pop_back()
    {
        if (empty()) {
            return false;
        }
        auto elem = std::destroy_at(&buffer_[buffer_idx(size_)]);
        size_--;
        return true;
    }

    bool pop_front()
    {
        if (empty()) {
            return false;
        }
        std::destroy_at(&buffer_[buffer_idx(size_)]);
        size_--;
        start_ = (start_ + 1) % N;
        return true;
    }

private:
    std::size_t buffer_idx(std::size_t pos) const { return (pos + start_) % N; }

    std::unique_ptr<T[]> buffer_;
    std::size_t size_ = 0;
    std::size_t start_ = 0;
};

#endif