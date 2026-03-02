#pragma once

#include "Core/Asserts.hpp"
#include "Shaders/ShaderDataType.hpp"

#include <vector>
#include <string>

namespace Kerberos
{
	struct BufferElement
	{
		std::string Name;
		ShaderDataType Type;
		uint32_t Size;
		uint32_t Offset;
		bool Normalized;

		BufferElement() = default;

		BufferElement(const ShaderDataType type, std::string name, const bool normalized = false)
			: Name(std::move(name)), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized)
		{
		}

		uint32_t GetComponentCount() const
		{
			switch (Type)
			{
				case ShaderDataType::None:     return 0;
				case ShaderDataType::Float:    return 1;
				case ShaderDataType::Float2:   return 2;
				case ShaderDataType::Float3:   return 3;
				case ShaderDataType::Float4:   return 4;
				case ShaderDataType::Mat3:     return 3 * 3;
				case ShaderDataType::Mat4:     return 4 * 4;
				case ShaderDataType::Int:      return 1;
				case ShaderDataType::Int2:     return 2;
				case ShaderDataType::Int3:     return 3;
				case ShaderDataType::Int4:     return 4;
				case ShaderDataType::Bool:     return 1;
			}

			KBR_CORE_ASSERT(false, "Unknown ShaderDataType!");
			return 0;
		}
	};

	class BufferLayout
	{
	public:
		BufferLayout() = default;

		BufferLayout(const std::initializer_list<BufferElement>& elements)
			: m_Elements(elements), m_Stride(0)
		{
			CalculateOffsetsAndStride();
		}

		const std::vector<BufferElement>& GetElements() const { return m_Elements; }
		uint32_t GetStride() const { return m_Stride; }

		std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
		std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
		std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
		std::vector<BufferElement>::const_iterator end() const { return m_Elements.end(); }

	private:
		void CalculateOffsetsAndStride()
		{
			uint32_t offset = 0;
			m_Stride = 0;
			for (auto& elem : m_Elements)
			{
				elem.Offset = offset;
				offset += elem.Size;
				m_Stride += elem.Size;
			}
		}

	private:
		std::vector<BufferElement> m_Elements;
		uint32_t m_Stride;
	};
}