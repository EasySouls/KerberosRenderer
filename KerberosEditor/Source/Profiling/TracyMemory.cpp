#ifdef KBR_ENABLE_TRACY

#include <tracy/Tracy.hpp>

#include <cstdlib>
#include <new>

namespace
{
#ifdef KBR_ENABLE_TRACY_MEMORY_CALLSTACKS
    // Capturing a callstack for every allocation is extremely expensive and is
    // only useful for focused allocation investigations. Keep it opt-in.
    constexpr int CallstackDepth = 10;

    void RecordAllocation(void* ptr, const std::size_t size) noexcept
    {
        TracyAllocS(ptr, size, CallstackDepth);
    }

    void RecordFree(void* ptr) noexcept
    {
        TracyFreeS(ptr, CallstackDepth);
    }
#else
    void RecordAllocation(const void* ptr, const std::size_t size) noexcept
    {
        TracyAlloc(ptr, size);
    }

    void RecordFree(const void* ptr) noexcept
    {
        TracyFree(ptr);
    }
#endif

    void* Allocate(const std::size_t size)
    {
        const auto ptr = std::malloc(size);
        if (!ptr)
            throw std::bad_alloc();

        RecordAllocation(ptr, size);
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

        RecordAllocation(ptr, size);
        return ptr;
    }

    void Deallocate(void* ptr) noexcept
    {
        if (!ptr)
            return;

        RecordFree(ptr);
        std::free(ptr);
    }

    void DeallocateAligned(void* ptr) noexcept
    {
        if (!ptr)
            return;

        RecordFree(ptr);
#ifdef _MSC_VER
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }
}

void* operator new(const std::size_t size)
{
    return Allocate(size);
}

void* operator new[](const std::size_t size)
{
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
    Deallocate(ptr);
}

void operator delete[](void* ptr) noexcept
{
    Deallocate(ptr);
}

void operator delete(void* ptr, const std::size_t) noexcept
{
    Deallocate(ptr);
}

void operator delete[](void* ptr, const std::size_t) noexcept
{
    Deallocate(ptr);
}

void* operator new(const std::size_t size, const std::align_val_t alignment)
{
    return AllocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](const std::size_t size, const std::align_val_t alignment)
{
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
    DeallocateAligned(ptr);
}

void operator delete[](void* ptr, const std::align_val_t) noexcept
{
    DeallocateAligned(ptr);
}

void operator delete(void* ptr,
                     const std::size_t,
                     const std::align_val_t) noexcept
{
    DeallocateAligned(ptr);
}

void operator delete[](void* ptr,
                       const std::size_t,
                       const std::align_val_t) noexcept
{
    DeallocateAligned(ptr);
}

#endif
