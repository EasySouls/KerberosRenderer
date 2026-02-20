#pragma once

#include <memory>

namespace kbr
{
	template<typename T>
	using Owner = std::unique_ptr<T>;

	template<typename T, typename ... Args>
	constexpr Owner<T> CreateOwner(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}
}