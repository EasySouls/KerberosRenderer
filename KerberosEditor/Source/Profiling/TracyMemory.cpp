#ifdef KBR_ENABLE_TRACY

#include <tracy/Tracy.hpp>

#include <cstdlib>
#include <new>
#include <mutex>

static std::mutex memoryLock;

constexpr int callstackDepth = 10;

namespace
{
    void* Allocate(const std::size_t size)
    {
        const auto ptr = std::malloc(size);
        if (!ptr)
            throw std::bad_alloc();

        TracyAllocS(ptr, size, callstackDepth);
        return ptr;
    }

    void* AllocateAligned(const std::size_t size, const std::size_t alignment)
    {
#ifdef _MSC_VER
        const auto ptr = _aligned_malloc(size, alignment);
#else
        void* ptr = nullptr;
        if (posix_memalign(&ptr, alignment, size) != 0)
            ptr = nullptr;
#endif
        if (!ptr)
            throw std::bad_alloc();

        TracyAllocS(ptr, size, callstackDepth);
        return ptr;
    }

    void Deallocate(void* ptr) noexcept
    {
        if (!ptr)
            return;

        TracyFreeS(ptr, callstackDepth);
#ifdef _MSC_VER
        std::free(ptr);
#else
        std::free(ptr);
#endif
    }

    void DeallocateAligned(void* ptr) noexcept
    {
        if (!ptr)
            return;

        TracyFreeS(ptr, callstackDepth);
#ifdef _MSC_VER
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }
}

void* operator new(const std::size_t size)
{
    std::lock_guard lock(memoryLock);
    return Allocate(size);
}

void* operator new[](const std::size_t size)
{
    std::lock_guard lock(memoryLock);
    return Allocate(size);
}

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void operator delete(void* ptr) noexcept
{
    std::lock_guard lock(memoryLock);
    Deallocate(ptr);
}

void operator delete[](void* ptr) noexcept
{
    std::lock_guard lock(memoryLock);
    Deallocate(ptr);
}

void operator delete(void* ptr, const std::size_t) noexcept
{
    std::lock_guard lock(memoryLock);
    Deallocate(ptr);
}

void operator delete[](void* ptr, const std::size_t) noexcept
{
    std::lock_guard lock(memoryLock);
    Deallocate(ptr);
}

void* operator new(const std::size_t size, const std::align_val_t alignment)
{
    std::lock_guard lock(memoryLock);
    return AllocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](const std::size_t size, const std::align_val_t alignment)
{
    std::lock_guard lock(memoryLock);
    return AllocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new(const std::size_t size,
                   const std::align_val_t alignment,
                   const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size, alignment);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void* operator new[](const std::size_t size,
                     const std::align_val_t alignment,
                     const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size, alignment);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void operator delete(void* ptr, const std::align_val_t) noexcept
{
    std::lock_guard lock(memoryLock);
    DeallocateAligned(ptr);
}

void operator delete[](void* ptr, const std::align_val_t) noexcept
{
    std::lock_guard lock(memoryLock);
    DeallocateAligned(ptr);
}

void operator delete(void* ptr,
                     const std::size_t,
                     const std::align_val_t) noexcept
{
    std::lock_guard lock(memoryLock);
    DeallocateAligned(ptr);
}

void operator delete[](void* ptr,
                       const std::size_t,
                       const std::align_val_t) noexcept
{
    std::lock_guard lock(memoryLock);
    DeallocateAligned(ptr);
}

#endif