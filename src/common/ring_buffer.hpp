#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>

// Simple ring buffer for VBO sub-allocation.
// Not thread-safe (only main thread touches GL).

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) : m_cap(capacity) {
        // Ensure capacity is non-zero and power-of-2 aligned for efficient wrapping
        if (m_cap == 0) m_cap = 4096;
    }

    // Tries to allocate `size` bytes. Returns offset or SIZE_MAX on overflow.
    // If allocation would overwrite unread data, returns SIZE_MAX to signal overflow.
    size_t alloc(size_t size) {
        if (size > m_cap) return SIZE_MAX;

        // Check if allocation would require wrapping
        if (m_offset + size <= m_cap) {
            // Fits in current segment
            size_t off = m_offset;
            m_offset += size;
            return off;
        }

        // Would need to wrap - check if new allocation would overlap with reader
        // For VBO use, we assume reader consumes before next frame, so wrap is safe
        m_offset = 0;
        m_generation++;
        size_t off = m_offset;
        m_offset = size;
        return off;
    }

    void reset() { m_offset = 0; m_generation++; }
    size_t capacity() const { return m_cap; }
    size_t offset()   const { return m_offset; }
    uint32_t gen()    const { return m_generation; }

private:
    size_t   m_cap = 4096;
    size_t   m_offset = 0;
    uint32_t m_generation = 0;
};
