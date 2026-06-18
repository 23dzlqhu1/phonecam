#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace phonecam {

// Thread-safe bounded queue with drop-oldest policy.
// When full, push() discards the oldest item to make room.
// pop() blocks until an item is available.
template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(int maxSize = 3) : m_maxSize(maxSize) {}

    // Push an item. If queue is full, drop the oldest.
    void push(T item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (static_cast<int>(m_queue.size()) >= m_maxSize) {
            m_queue.pop_front();  // Drop oldest
        }
        m_queue.push_back(std::move(item));
        m_cv.notify_one();
    }

    // Blocking pop. Returns std::nullopt only if the queue is shut down.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_shutdown; });
        if (m_queue.empty()) return std::nullopt;
        T item = std::move(m_queue.front());
        m_queue.pop_front();
        return item;
    }

    // Non-blocking try-pop
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return std::nullopt;
        T item = std::move(m_queue.front());
        m_queue.pop_front();
        return item;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
        m_cv.notify_all();
    }

    int size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_queue.size());
    }

private:
    std::deque<T> m_queue;
    int m_maxSize;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_shutdown = false;
};

} // namespace phonecam
