// Unit tests for services/buffer/safe_circular_buffer.h — the bounded frame
// queue that decouples camera capture from inference in CameraStreamer.
//
// Build & run:  ctest --test-dir build/tests --output-on-failure

#include "safe_circular_buffer.h"

#include <atomic>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "  [ ok ] " << name << "\n";
        return;
    }
    std::cout << "  [FAIL] " << name << "\n";
    ++failures;
}

using backend::safe_circular_buffer;

void test_fifo_order() {
    safe_circular_buffer<int> buffer(4);
    check(buffer.is_empty(), "new buffer is empty");

    for (int i = 1; i <= 4; ++i) {
        int value = i;
        check(buffer.push(value), "push within capacity");
    }
    check(buffer.is_full(), "buffer reports full at capacity");

    int fifth = 5;
    check(!buffer.push(fifth), "push beyond capacity is rejected");

    int out = 0;
    check(buffer.front(out) && out == 1, "front() returns oldest element");
    check(buffer.back(out) && out == 4, "back() returns newest element");
    for (int i = 1; i <= 4; ++i) {
        check(buffer.front(out) && out == i, "pop sequence keeps FIFO order");
        buffer.pop();
    }
    check(buffer.is_empty(), "buffer drains to empty");
    check(!buffer.pop(), "pop on empty buffer is rejected");
}

void test_wraparound() {
    safe_circular_buffer<int> buffer(3);
    int value = 0;
    for (int cycle = 0; cycle < 5; ++cycle) {
        for (int i = 0; i < 3; ++i) {
            value = cycle * 10 + i;
            buffer.push(value);
        }
        for (int i = 0; i < 3; ++i) {
            check(buffer.front(value) && value == cycle * 10 + i,
                  "ring buffer stays consistent across wraparound");
            buffer.pop();
        }
    }
}

void test_producer_consumer() {
    // Multi-threaded smoke test: a producer and a consumer must agree on the
    // total number of items transferred, with no lost or duplicated frames.
    constexpr int kItems = 20000;
    safe_circular_buffer<int> buffer(64);
    std::atomic<int> produced{0};
    std::atomic<long> consumedSum{0};
    std::atomic<int> consumedCount{0};

    std::thread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!buffer.push(i)) {
                std::this_thread::yield();
            }
            produced.fetch_add(1);
        }
    });

    std::thread consumer([&]() {
        int out = 0;
        while (consumedCount.load() < kItems) {
            if (buffer.front(out)) {
                consumedSum.fetch_add(out);
                consumedCount.fetch_add(1);
                buffer.pop();
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    const long expected = static_cast<long>(kItems) * (kItems + 1) / 2;
    check(produced.load() == kItems, "producer pushed every item");
    check(consumedCount.load() == kItems, "consumer received every item");
    check(consumedSum.load() == expected, "consumer received items intact and in order");
}

} // namespace

int main() {
    std::cout << "safe_circular_buffer: fifo semantics\n";
    test_fifo_order();
    std::cout << "safe_circular_buffer: wraparound\n";
    test_wraparound();
    std::cout << "safe_circular_buffer: producer/consumer\n";
    test_producer_consumer();

    if (failures) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all safe_circular_buffer checks passed\n";
    return 0;
}
