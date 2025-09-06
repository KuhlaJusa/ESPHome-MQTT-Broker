#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility> // for std::move
#include <atomic>

namespace esphome {
namespace asyncbroker {


template<typename T, size_t Capacity>
class SPSCQueue {
public:
    SPSCQueue() : head_(0), tail_(0) {}
    void clear() {
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
    }
    // Producer: enqueue (move)
    bool push(T&& item) {
        size_t next_head = (head_ + 1) % Capacity;
        if (next_head == tail_.load(std::memory_order_acquire)) {
            // queue full
            return false;
        }
        buffer_[head_] = std::move(item);
        head_ = next_head;
        return true;
    }

    // Producer: enqueue (copy)
    bool push(const T& item) {
        size_t next_head = (head_ + 1) % Capacity;
        if (next_head == tail_.load(std::memory_order_acquire)) {
            // queue full
            return false;
        }
        buffer_[head_] = item;
        head_ = next_head;
        return true;
    }

    // Consumer: dequeue
    void pop() {
        size_t tail = tail_.load(std::memory_order_acquire);
        if (tail != head_) {
            tail_.store((tail + 1) % Capacity, std::memory_order_release);
        }
    }
    bool pop(T& out) {
        size_t tail = tail_.load(std::memory_order_acquire);
        if (tail == head_) {
            // queue empty
            return false;
        }
        out = std::move(buffer_[tail]);
        tail_.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

    void clear_with_destruct() {
        T tmp;
        while (pop(tmp)) {
            // tmp goes out of scope, destructor called
        }
    }
    // Peek without removing (consumer can iterate)
    template<typename Func>
    void for_each(Func&& f) {
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t h = head_;
        while (tail != h) {
            f(buffer_[tail]);
            tail = (tail + 1) % Capacity;
        }
    }

    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_;
    }

    bool full() const {
        return ((head_ + 1) % Capacity) == tail_.load(std::memory_order_acquire);
    }

    size_t size() const {
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t h = head_;
        if (h >= tail) return h - tail;
        return Capacity - (tail - h);
    }

    T& peek(size_t offset) {
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t pos = (tail + offset) % Capacity;
        return buffer_[pos];
    }

private:
    std::array<T, Capacity> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

} //namespace asyncbroker
} //namespace esphome