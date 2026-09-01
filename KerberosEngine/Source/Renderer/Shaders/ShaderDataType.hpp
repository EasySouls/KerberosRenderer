#pragma once

#include <cstdint>

namespace Kerberos {

enum class ShaderDataType : std::uint8_t
{
	None = 0,
	Float, Float2, Float3, Float4,
	Mat3, Mat4,
	Int, Int2, Int3, Int4,
	Bool
};

uint32_t ShaderDataTypeSize(ShaderDataType type);

}
