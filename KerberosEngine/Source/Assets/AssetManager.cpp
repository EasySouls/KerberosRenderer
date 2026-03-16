#include "kbrpch.hpp"
#include "AssetManager.hpp"

namespace Kerberos
{
	Ref<Texture2D> AssetManager::GetDefaultTexture2D()
	{
		constexpr std::array<uint8_t, 4> albedoBuffer = { 255, 255, 255, 255 };
		TextureSpecification albedoSpec{};
		albedoSpec.Width = 1;
		albedoSpec.Height = 1;
		albedoSpec.Format = ImageFormat::RGBA8;
		const Buffer albedoBufferStruct{ sizeof(uint8_t) * albedoBuffer.size() };
		std::memcpy(albedoBufferStruct.Data, albedoBuffer.data(), albedoBufferStruct.Size);
		return Texture2D::FromBuffer(albedoSpec, albedoBufferStruct);
	}

	Ref<Mesh> AssetManager::GetDefaultCubeMesh() 
	{
		// TODO: Package a cube.gltf alongside the editor or create a mesh programmatically
		throw std::logic_error("Not implemented");
	}
}
