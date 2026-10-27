
#ifndef SAFE_CIRCULAR_BUFFER_H
#define SAFE_CIRCULAR_BUFFER_H

#include <mutex>
#include <vector>

namespace veera {
template <class T>
class safe_circular_buffer {
 private:
    std::mutex lock_mutex_;
    std::vector<T> queue_;
    unsigned int size_;
    unsigned int head_;
    unsigned int tail_;
    unsigned int count_;  // Keeps track of the number of elements in the buffer

    // Private helper functions for checking conditions
    bool is_empty_unlocked() const { return count_ == 0; }

    bool is_full_unlocked() const { return count_ == size_; }

 public:
    explicit safe_circular_buffer(unsigned int queue_length = 10)
        : size_(queue_length), head_(0), tail_(0), count_(0) {
        queue_.resize(queue_length);
    }

    bool is_empty() {
        std::lock_guard<std::mutex> lock_guard(lock_mutex_);
        return is_empty_unlocked();
    }

    bool is_full() {
        std::lock_guard<std::mutex> lock_guard(lock_mutex_);
        return is_full_unlocked();
    }

    bool push(T& value) {
        std::lock_guard<std::mutex> lock_guard(lock_mutex_);
        if (is_full_unlocked())  // Inline check for full condition using the unlocked helper
            return false;  // Queue is full!

        queue_[head_] = value;
        head_ = (head_ + 1) % size_;
        count_++;
        return true;
    }

    bool front(T& variable) {
        std::lock_guard<std::mutex> lock_guard(lock_mutex_);
        if (is_empty_unlocked())  // Inline check for empty condition using the unlocked helper
            return false;  // Queue is empty!
        variable = queue_[tail_];
        return true;
    }

    bool back(T& variable) {
        std::lock_guard<std::mutex> lock_guard(lock_mutex_);
        if (is_empty_unlocked())
            return false;
        unsigned int newest = (head_ - 1 + size_) % size_;
        variable = queue_[newest];
        return true;
    }

    bool pop() {
        std::lock_guard<std::mutex> lock_guard(lock_mutex_);
        if (is_empty_unlocked())  // Inline check for empty condition using the unlocked helper
            return false;  // Queue is empty!
        tail_ = (tail_ + 1) % size_;
        count_--;
        return true;
    }
};
}

#endif  // SAFE_CIRCULAR_BUFFER_H
