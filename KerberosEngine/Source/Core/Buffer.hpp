#pragma once

#include <cstdint>
#include <cstring>

namespace Kerberos
{
	struct Buffer
	{
		uint8_t* Data = nullptr;
		uint64_t Size = 0;

		Buffer() = default;
		~Buffer()
		{
			Release();
		}

		explicit Buffer(const uint64_t size)
		{
			Allocate(size);
		}

		Buffer(const Buffer&) = default;
		Buffer(Buffer&& other) noexcept = delete;

		Buffer& operator=(const Buffer& other) = default;
		Buffer& operator=(Buffer&& other) noexcept = delete;

		static Buffer Copy(const Buffer& other)
		{
			const Buffer result(other.Size);
			std::memcpy(result.Data, other.Data, other.Size);
			return result;
		}

		void Allocate(const uint64_t size)
		{
			Release();

			Data = new uint8_t[size];
			Size = size;
		}

		void Release()
		{
			delete[] Data;
			Data = nullptr;
			Size = 0;
		}

		template<typename T>
		T* As()
		{
			return static_cast<T*>(Data);
		}

		explicit(false) operator bool() const
		{
			return static_cast<bool>(Data);
		}

	};
}
