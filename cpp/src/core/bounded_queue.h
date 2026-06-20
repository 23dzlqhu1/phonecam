#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace phonecam {

// Queue overflow policy
enum class QueuePolicy {
    DropOldest,  // Drop oldest item when full (for decoded image display queue)
    NoDrop,      // Reject push when full — caller must handle overflow (for raw H264 queue)
};

// Thread-safe bounded queue with configurable overflow policy.
template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(int maxSize = 3, QueuePolicy policy = QueuePolicy::DropOldest)
        : m_maxSize(maxSize), m_policy(policy) {}

    // Push an item.
    // DropOldest: if full, drop the oldest to make room (always succeeds).
    // NoDrop: if full, drop the NEW item (reject push). Returns true if accepted.
    bool push(T item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (static_cast<int>(m_queue.size()) >= m_maxSize) {
            if (m_policy == QueuePolicy::NoDrop) {
                return false;  // Reject: caller must handle overflow
            }
            m_queue.pop_front();  // DropOldest: make room
        }
        m_queue.push_back(std::move(item));
        m_cv.notify_one();
        return true;
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
    QueuePolicy m_policy;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_shutdown = false;
};

} // namespace phonecam
