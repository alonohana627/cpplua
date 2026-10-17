#include "gc/pool.hpp"

#include <new>
#include <utility>

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

} // namespace

void* Pool::allocate(std::size_t size) {
    if (sanitized || size > max_pooled || size == 0) {
        return ::operator new(size == 0 ? 1 : size);
    }
    const std::size_t size_class = (round_up(size, granule) / granule) - 1;
    if (FreeBlock* block = free_lists_[size_class]) {
        free_lists_[size_class] = block->next;
        return block;
    }
    return refill(size_class);
}

void Pool::free(void* block, std::size_t size) {
    if (sanitized || size > max_pooled || size == 0) {
        ::operator delete(block);
        return;
    }
    const std::size_t size_class = (round_up(size, granule) / granule) - 1;
    auto* free_block = static_cast<FreeBlock*>(block);
    free_block->next = free_lists_[size_class];
    free_lists_[size_class] = free_block;
}

void* Pool::refill(std::size_t size_class) {
    const std::size_t block_size = (size_class + 1) * granule;
    if (cursor_ == nullptr || std::cmp_less(page_end_ - cursor_, block_size)) {
        // A new page; what is left of the old one stays unused (at most one
        // block's worth).
        pages_.push_back(std::make_unique_for_overwrite<std::byte[]>( // NOLINT(*-avoid-c-arrays)
            page_size));
        cursor_ = pages_.back().get();
        page_end_ = cursor_ + page_size;
    }
    void* block = cursor_;
    cursor_ += block_size;
    return block;
}

} // namespace cpplua
