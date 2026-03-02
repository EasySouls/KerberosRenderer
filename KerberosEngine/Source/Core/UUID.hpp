#pragma once

#include <xhash>

namespace Kerberos
{
	class UUID
	{
	public:
		/**
		* Creates a random 64-bit UUID.
		*/
		UUID();

		/**
		* @brief	Creates a UUID with the given value.
		* It is the caller's job top make sure that the value is not used.
		*/
		explicit UUID(uint64_t uuid);

		explicit(false) operator uint64_t() const { return m_UUID; }

		bool operator==(const UUID& other) const
		{
			return m_UUID == other.m_UUID;
		}

		bool operator!=(const UUID& other) const
		{
			return !(*this == other);
		}

		static UUID Invalid()
		{
			return UUID(0);
		}

		inline bool IsValid() const
		{
			return m_UUID != 0;
		}

	private:
		uint64_t m_UUID;
	};
}

template<>
struct std::hash<Kerberos::UUID>
{
	std::size_t operator()(const Kerberos::UUID& uuid) const noexcept
	{
		return hash<uint64_t>()((uint64_t)uuid);
	}
};
