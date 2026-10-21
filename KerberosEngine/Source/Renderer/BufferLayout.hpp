#pragma once

#include "Core/Core.hpp"
#include "Shaders/ShaderDataType.hpp"

#include <vector>
#include <string>

namespace Kerberos {

struct BufferElement
{
	std::string Name;
	ShaderDataType Type;
	uint32_t Size;
	uint32_t Offset;
	bool Normalized;

	BufferElement() = default;

	BufferElement(ShaderDataType type, std::string name, bool normalized = false);

	uint32_t GetComponentCount() const;
};

class BufferLayout
{
public:
	BufferLayout() = default;

	BufferLayout(const std::initializer_list<BufferElement>& elements);

	const std::vector<BufferElement>& GetElements() const { return m_Elements; }
	uint32_t GetStride() const { return m_Stride; }

	std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
	std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
	std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
	std::vector<BufferElement>::const_iterator end() const { return m_Elements.end(); }

private:
	void CalculateOffsetsAndStride();

private:
	std::vector<BufferElement> m_Elements;
	uint32_t m_Stride;
};

}