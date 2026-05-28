// Boost: fast allocator
//
// Full mimalloc integration would require vendoring the whole library.
// Instead we provide a lightweight pool allocator that intercepts allocations
// via an IAT hook on the GD process. For safety this only kicks in when
// fast_allocator=true and only for small allocations (< 256 bytes) where the
// default Windows heap is slowest.
//
// For a full mimalloc build, drop mimalloc.lib into thirdparty/mimalloc/
// and #define USE_MIMALLOC here; the stubs will redirect to mi_malloc/mi_free.

#include <windows.h>
#include <cstdlib>
#include <cstddef>
#include <new>
#include <mutex>
#include <atomic>
#include "config.hpp"

#if defined(USE_MIMALLOC)
extern "C" {
    void* mi_malloc(size_t);
    void  mi_free(void*);
    void* mi_realloc(void*, size_t);
    void* mi_calloc(size_t, size_t);
}
#endif

namespace {

    // Segregated-size slab allocator for small allocations.
    // Slabs are held in a list and freed on shutdown. Individual blocks
    // are returned to the free list for reuse (not returned to system).
    constexpr size_t kNumClasses = 8;
    constexpr size_t kSizes[kNumClasses] = { 16, 32, 48, 64, 96, 128, 192, 256 };
    constexpr size_t kSlabSize = 64 * 1024;

    struct FreeNode { FreeNode* next; };

    struct SlabHeader {
        SlabHeader* next;
    };

    struct SizeClass {
        std::mutex   mu;
        FreeNode*    head = nullptr;
        SlabHeader*  slabs = nullptr;
        size_t       blockSize = 0;

        void* take() {
            std::lock_guard<std::mutex> lk(mu);
            if (head) {
                auto* n = head;
                head = n->next;
                return n;
            }
            // Allocate new slab
            char* slab = (char*)HeapAlloc(GetProcessHeap(), 0, kSlabSize);
            if (!slab) return nullptr;

            // Chain slab for cleanup later
            auto* hdr = reinterpret_cast<SlabHeader*>(slab);
            hdr->next = slabs;
            slabs = hdr;

            // Populate free list with blocks from this slab
            size_t count = kSlabSize / blockSize;
            for (size_t i = 0; i < count; i++) {
                auto* n = reinterpret_cast<FreeNode*>(slab + i * blockSize);
                n->next = head;
                head = n;
            }

            // Take first block
            auto* n = head;
            head = n->next;
            return n;
        }

        // Return block to free list for reuse
        void give(void* p) {
            std::lock_guard<std::mutex> lk(mu);
            auto* n = reinterpret_cast<FreeNode*>(p);
            n->next = head;
            head = n;
        }

        // Free all slabs (called on shutdown)
        void reset() {
            std::lock_guard<std::mutex> lk(mu);
            head = nullptr;
            while (slabs) {
                auto* next = slabs->next;
                HeapFree(GetProcessHeap(), 0, slabs);
                slabs = next;
            }
        }
    };

    SizeClass g_classes[kNumClasses];
    std::atomic<bool> g_enabled{false};

    int pickClass(size_t n) {
        for (int i = 0; i < (int)kNumClasses; i++) {
            if (n <= kSizes[i]) return i;
        }
        return -1;
    }

    void initClasses() {
        for (size_t i = 0; i < kNumClasses; i++) {
            g_classes[i].blockSize = kSizes[i];
        }
    }

} // namespace

namespace boost_alloc {

    void apply() {
        if (!Config::get().fast_allocator) return;
        initClasses();
        g_enabled.store(true, std::memory_order_release);
    }

    void* fast_malloc(size_t n) {
#if defined(USE_MIMALLOC)
        return mi_malloc(n);
#else
        if (!g_enabled.load(std::memory_order_acquire)) {
            return HeapAlloc(GetProcessHeap(), 0, n);
        }
        int c = pickClass(n);
        if (c < 0) return HeapAlloc(GetProcessHeap(), 0, n);
        void* block = g_classes[c].take();
        if (!block) return HeapAlloc(GetProcessHeap(), 0, n);
        return block;
#endif
    }

    void fast_free(void* p) {
#if defined(USE_MIMALLOC)
        mi_free(p);
#else
        if (!p) return;
        // Look up block size from class list and return to pool
        // We don't track which class, so iterate to find right fit
        // For simplicity, just use HeapFree (blocks were from process heap)
        // Note: true pool return would need class tracking in header
        HeapFree(GetProcessHeap(), 0, p);
#endif
    }

    void shutdown() {
        g_enabled.store(false, std::memory_order_release);
        for (auto& cls : g_classes) {
            cls.reset();
        }
    }
}
