#include "kbrpch.hpp"
#include "SMAAUtils.hpp"

namespace
{
#define AREATEX_WIDTH 160
#define AREATEX_HEIGHT 560
#define AREATEX_PITCH (AREATEX_WIDTH * 2)
#define AREATEX_SIZE (AREATEX_HEIGHT * AREATEX_PITCH)
}

namespace Kerberos
{
	Ref<Texture2D> LoadSMAAAreaTexture() 
	{
		throw std::logic_error("Not implemented");
	}

	Ref<Texture2D> LoadSMAASearchTexture() 
	{
		throw std::logic_error("Not implemented");
	}
}
