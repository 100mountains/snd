// Fixed-capacity block event storage for realtime paths.
#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace snd {

template <typename Event, std::size_t Capacity>
class EventBuffer {
public:
    using value_type = Event;
    using iterator = Event*;
    using const_iterator = const Event*;

    EventBuffer() = default;
    EventBuffer(std::initializer_list<Event> events)
    {
        for (const auto& event : events)
            (void)push_back(event);
    }

    bool push_back(const Event& event)
    {
        if (size_ == Capacity)
            return false;
        events_[size_++] = event;
        return true;
    }

    bool push_back(Event&& event)
    {
        if (size_ == Capacity)
            return false;
        events_[size_++] = std::move(event);
        return true;
    }

    void clear() noexcept { size_ = 0; }
    constexpr std::size_t capacity() const noexcept { return Capacity; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    Event& operator[](std::size_t index) noexcept { return events_[index]; }
    const Event& operator[](std::size_t index) const noexcept { return events_[index]; }
    Event& back() noexcept { return events_[size_ - 1]; }
    const Event& back() const noexcept { return events_[size_ - 1]; }

    iterator begin() noexcept { return events_.data(); }
    iterator end() noexcept { return events_.data() + size_; }
    const_iterator begin() const noexcept { return events_.data(); }
    const_iterator end() const noexcept { return events_.data() + size_; }

private:
    std::array<Event, Capacity> events_{};
    std::size_t size_ = 0;
};

} // namespace snd
