#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace cpplua {

// Memory for the collector's objects (docs/plan/vm.md: size-class pages,
// measured 1.9 to 3.2 times faster than malloc with a list of objects).
//
// Small blocks are rounded up to a multiple of 16 bytes, and each size has
// its own free list. A free list that runs dry is refilled from the current
// 64 KB page, and a new page is taken when that runs out. Freed blocks go back
// to their size's list. Big blocks come from operator new.
//
// Built with AddressSanitizer, every block comes from operator new, so the
// sanitizer can still see use-after-free and overflows in pooled objects.
class Pool {
public:
    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;
    ~Pool() = default;

    // `size` bytes, aligned to 16. `size` must be given again to free().
    void* allocate(std::size_t size);
    void free(void* block, std::size_t size);

    static constexpr std::size_t granule = 16;
    static constexpr std::size_t max_pooled = 512;

private:
    static constexpr std::size_t class_count = max_pooled / granule;
    static constexpr std::size_t page_size = std::size_t{64} * 1024;

    struct FreeBlock {
        FreeBlock* next;
    };

    void* refill(std::size_t size_class);

    std::array<FreeBlock*, class_count> free_lists_{};
    std::vector<std::unique_ptr<std::byte[]>> pages_; // NOLINT(*-avoid-c-arrays): raw pages
    std::byte* cursor_ = nullptr;                     // the unused rest of the newest page
    std::byte* page_end_ = nullptr;
};

} // namespace cpplua
