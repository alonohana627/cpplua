#include "gc/pool.hpp"

#include <bit>
#include <cstdint>
#include <new>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace cpplua {

namespace {

#ifdef __has_feature
#if __has_feature(address_sanitizer)
constexpr bool sanitized = true;
#else
constexpr bool sanitized = false;
#endif
#elifdef __SANITIZE_ADDRESS__
constexpr bool sanitized = true;
#else
constexpr bool sanitized = false;
#endif

constexpr std::size_t round_up(std::size_t size, std::size_t to) {
    return (size + to - 1) / to * to;
}

// A page from the system, aligned to its size, so a block finds its page by
// clearing the low bits of its address.
void* system_page(std::size_t size) {
#ifdef _WIN32
    // VirtualAlloc's granularity is 64 KB: its blocks are aligned to that.
    static_assert(Pool::page_size == 64 * 1024);
    void* page = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (page == nullptr) {
        throw std::bad_alloc{};
    }
    return page;
#else
    // Twice the size, then the parts before and after an aligned page go
    // back.
    void* mapped =
        mmap(nullptr, 2 * size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        throw std::bad_alloc{};
    }
    const auto start = std::bit_cast<std::uintptr_t>(mapped);
    const std::uintptr_t aligned = (start + size - 1) & ~(std::uintptr_t{size} - 1);
    if (aligned > start) {
        munmap(mapped, aligned - start);
    }
    if (const std::uintptr_t after = start + (2 * size) - (aligned + size); after > 0) {
        munmap(std::bit_cast<void*>(aligned + size), after);
    }
    return std::bit_cast<void*>(aligned);
#endif
}

void release_system_page(void* page, std::size_t size) {
#ifdef _WIN32
    static_cast<void>(size);
    VirtualFree(page, 0, MEM_RELEASE);
#else
    munmap(page, size);
#endif
}

} // namespace

Pool::~Pool() {
    while (all_pages_ != nullptr) {
        Page* page = all_pages_;
        all_pages_ = page->next_page;
        release_system_page(page, page_size);
    }
}

Pool::Page* Pool::page_of(void* block) {
    return std::bit_cast<Page*>(std::bit_cast<std::uintptr_t>(block) &
                                ~(std::uintptr_t{page_size} - 1));
}

void* Pool::allocate(std::size_t size) {
    if (sanitized || size > max_pooled || size == 0) {
        return ::operator new(size == 0 ? 1 : size);
    }
    const std::size_t size_class = (round_up(size, granule) / granule) - 1;
    Page* page = pages_with_room_[size_class];
    if (page == nullptr) [[unlikely]] {
        page = new_page(size_class);
    }
    void* block = nullptr;
    if (FreeBlock* free_block = page->free) {
        page->free = free_block->next;
        block = free_block;
    } else {
        block = page->bump;
        page->bump += page->block_size;
    }
    ++page->live;
    if (page->free == nullptr && page->bump + page->block_size > page->end) {
        unlink(page); // full
    }
    return block;
}

void Pool::free(void* block, std::size_t size) {
    if (sanitized || size > max_pooled || size == 0) {
        ::operator delete(block);
        return;
    }
    Page* page = page_of(block);
    auto* free_block = static_cast<FreeBlock*>(block);
    free_block->next = page->free;
    page->free = free_block;
    if (--page->live == 0) {
        release(page);
    } else if (!page->listed) {
        link(page); // it has room again
    }
}

Pool::Page* Pool::new_page(std::size_t size_class) {
    Page* page = spare_;
    if (page != nullptr) {
        spare_ = page->next;
        --spare_count_;
    } else {
        page = static_cast<Page*>(system_page(page_size));
        ++page_count_;
        page->next_page = all_pages_;
        page->prev_page = nullptr;
        if (all_pages_ != nullptr) {
            all_pages_->prev_page = page;
        }
        all_pages_ = page;
    }
    auto* bytes = std::bit_cast<std::byte*>(page);
    page->free = nullptr;
    page->bump = bytes + header_size;
    page->end = bytes + page_size;
    page->live = 0;
    page->size_class = static_cast<std::uint32_t>(size_class);
    page->block_size = static_cast<std::uint32_t>((size_class + 1) * granule);
    page->listed = false;
    link(page);
    return page;
}

void Pool::release(Page* page) {
    // A page with no block in use: kept for any size if there are few
    // spares, else back to the system.
    if (page->listed) {
        unlink(page);
    }
    if (spare_count_ < max_spare_pages) {
        page->next = spare_;
        spare_ = page;
        ++spare_count_;
        return;
    }
    if (page->prev_page != nullptr) {
        page->prev_page->next_page = page->next_page;
    } else {
        all_pages_ = page->next_page;
    }
    if (page->next_page != nullptr) {
        page->next_page->prev_page = page->prev_page;
    }
    --page_count_;
    release_system_page(page, page_size);
}

void Pool::link(Page* page) {
    Page*& head = pages_with_room_[page->size_class];
    page->prev = nullptr;
    page->next = head;
    if (head != nullptr) {
        head->prev = page;
    }
    head = page;
    page->listed = true;
}

void Pool::unlink(Page* page) {
    if (page->prev != nullptr) {
        page->prev->next = page->next;
    } else {
        pages_with_room_[page->size_class] = page->next;
    }
    if (page->next != nullptr) {
        page->next->prev = page->prev;
    }
    page->prev = nullptr;
    page->next = nullptr;
    page->listed = false;
}

} // namespace cpplua
