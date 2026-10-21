#include "BufferLayout.hpp"

#include <stdexcept>

import Kerberos;

namespace Kerberos {

BufferElement::BufferElement(const ShaderDataType type, std::string name, const bool normalized)
	: Name(std::move(name)), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized)
{
}

uint32_t BufferElement::GetComponentCount() const
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

    KBRAssert(false, "Unknown ShaderDataType!");
    return 0;
}

BufferLayout::BufferLayout(const std::initializer_list<BufferElement>& elements): m_Elements(elements), m_Stride(0)
{
    CalculateOffsetsAndStride();
}

void BufferLayout::CalculateOffsetsAndStride()
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

}