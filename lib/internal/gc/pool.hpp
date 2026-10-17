#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cpplua {

// Memory for the collector's objects (docs/plan/vm.md: size-class pages,
// measured 1.9 to 3.2 times faster than malloc with a list of objects).
//
// Small blocks are rounded up to a multiple of 16 bytes. Each 64 KB page,
// aligned to its size, holds blocks of one size only, and knows how many of
// them are in use: a freed block goes back to its page, and a page whose
// blocks are all free goes back to the system (a few are kept for reuse),
// so a program that moves from one object size to another does not keep the
// peak of every size at once. Big blocks come from operator new.
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
    ~Pool();

    // `size` bytes, aligned to 16. `size` must be given again to free().
    // Throws std::bad_alloc when the system has no memory.
    void* allocate(std::size_t size);
    void free(void* block, std::size_t size);

    // How many pages the pool holds from the system, the spare ones included
    // (for tests).
    [[nodiscard]] std::size_t page_count() const { return page_count_; }

    static constexpr std::size_t granule = 16;
    static constexpr std::size_t max_pooled = 512;
    static constexpr std::size_t page_size = std::size_t{64} * 1024;

private:
    static constexpr std::size_t class_count = max_pooled / granule;
    static constexpr std::size_t max_spare_pages = 16; // 1 MB kept for reuse

    struct FreeBlock {
        FreeBlock* next;
    };

    // The header at the start of each page.
    struct Page {
        FreeBlock* free = nullptr; // freed blocks of this page
        std::byte* bump = nullptr; // blocks never handed out start here
        std::byte* end = nullptr;
        Page* prev = nullptr; // in its size's list of pages with room
        Page* next = nullptr;
        Page* next_page = nullptr; // in the list of every page (for the destructor)
        Page* prev_page = nullptr;
        std::uint32_t live = 0; // blocks in use
        std::uint32_t block_size = 0;
        std::uint32_t size_class = 0;
        bool listed = false; // in its size's list
    };

    static constexpr std::size_t header_size = (sizeof(Page) + 63) / 64 * 64;

    static Page* page_of(void* block);
    Page* new_page(std::size_t size_class);
    void release(Page* page);
    void link(Page* page);
    void unlink(Page* page);

    std::array<Page*, class_count> pages_with_room_{};
    Page* all_pages_ = nullptr;
    Page* spare_ = nullptr; // empty pages kept for reuse, through `next`
    std::size_t spare_count_ = 0;
    std::size_t page_count_ = 0;
};

} // namespace cpplua
