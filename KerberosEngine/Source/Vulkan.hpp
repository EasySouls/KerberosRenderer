#pragma once

#ifdef _MSC_VER
    #pragma warning(push, 0)
#endif

#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
//#define VULKAN_HPP_NO_EXCEPTIONS
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif