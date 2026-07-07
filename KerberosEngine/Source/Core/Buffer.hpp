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

		explicit Buffer(const std::vector<uint8_t>& data)
		{
			Allocate(data.size());
			std::memcpy(Data, data.data(), Size);
		}

		Buffer(const Buffer&) = delete;
		Buffer(Buffer&& other) noexcept
			: Data(other.Data), Size(other.Size)
		{
			other.Data = nullptr;
			other.Size = 0;
		}

		Buffer& operator=(const Buffer& other) = delete;
		Buffer& operator=(Buffer&& other) noexcept
		{
			if (this == &other)
				return *this;

			Release();
			Data = other.Data;
			Size = other.Size;
			other.Data = nullptr;
			other.Size = 0;
			return *this;
		}

		static Buffer Copy(const Buffer& other)
		{
			Buffer result(other.Size);
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
