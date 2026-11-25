// A fixed-size ring of bytes, for Grok's audio between the network and the speaker.
// Used from one task only, so it needs no locking.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace grokcore {

class ByteRing {
public:
    explicit ByteRing(size_t capacity) : data_(new (std::nothrow) uint8_t[capacity]), capacity_(data_ ? capacity : 0) {}

    bool ok() const { return data_ != nullptr; }
    size_t capacity() const { return capacity_; }
    size_t size() const { return size_; }
    size_t room() const { return capacity_ - size_; }
    void clear() { head_ = size_ = 0; }

    // Appends up to len bytes; returns how many fitted.
    size_t write(const uint8_t* src, size_t len) {
        if (len > room()) len = room();
        if (len == 0) return 0;
        size_t tail = (head_ + size_) % capacity_;
        size_t done = 0;
        while (done < len) {
            const size_t run = std::min(len - done, capacity_ - tail);
            std::memcpy(data_.get() + tail, src + done, run);
            done += run;
            tail = (tail + run) % capacity_;
        }
        size_ += len;
        return len;
    }

    // Removes up to len bytes from the front into dst; returns how many.
    size_t read(uint8_t* dst, size_t len) {
        if (len > size_) len = size_;
        size_t done = 0;
        while (done < len) {
            const size_t run = std::min(len - done, capacity_ - head_);
            std::memcpy(dst + done, data_.get() + head_, run);
            done += run;
            head_ = (head_ + run) % capacity_;
        }
        size_ -= len;
        return len;
    }

private:
    std::unique_ptr<uint8_t[]> data_;
    size_t capacity_;
    size_t head_ = 0;
    size_t size_ = 0;
};

}  // namespace grokcore
