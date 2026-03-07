#pragma once

#include <utility>
#include <concepts>

template <typename Type>
concept Scopeable = std::equality_comparable<Type> && std::is_default_constructible_v<Type>;

template <Scopeable Type, typename Deleter>
class Scoped
{
public:
    Scoped(Scoped const&) = delete;
    auto operator=(Scoped const&) = delete;

    Scoped() = default;

    constexpr Scoped(Scoped&& rhs) noexcept
        : m_Value(std::exchange(rhs.m_Value, Type{}))
    {
    }

    constexpr auto operator=(Scoped&& rhs) noexcept -> Scoped&
    {
        if (&rhs != this) { std::swap(m_Value, rhs.m_Value); }
        return *this;
    }

    explicit(false) constexpr Scoped(Type t) : m_Value(std::move(t)) {}

    constexpr ~Scoped()
    {
        if (m_Value == Type{}) { return; }
        Deleter{}(m_Value);
    }

    [[nodiscard]]
    constexpr const Type& get() const { return m_Value; }
    [[nodiscard]]
    constexpr Type& get() { return m_Value; }

private:
    Type m_Value{};
};