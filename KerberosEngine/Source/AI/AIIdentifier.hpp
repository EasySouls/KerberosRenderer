#pragma once

#include <cstdint>
#include <string_view>

namespace Kerberos::AI {

using BlackboardKey = std::uint64_t;
using StateID		= std::uint64_t;

inline constexpr StateID InvalidState = 0;

constexpr std::uint64_t HashFnv1a(const std::string_view s) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : s)
        hash = (hash ^ static_cast<std::uint64_t>(c)) * 1099511628211ull;
    return hash;
}

constexpr BlackboardKey HashKey(const std::string_view s) noexcept
{
    return static_cast<BlackboardKey>(HashFnv1a(s));
}

constexpr StateID HashState(const std::string_view s) noexcept
{
    return static_cast<StateID>(HashFnv1a(s));
}

constexpr BlackboardKey operator""_bb(const char* str, const std::size_t len) noexcept
{
    return HashKey(std::string_view(str, len));
}

constexpr StateID operator""_id(const char* str, const std::size_t len) noexcept
{
    return HashState(std::string_view(str, len));
}

}
