#pragma once

#include "Texture.hpp"
#include "Core/Buffer.hpp"

#include <filesystem>

namespace Kerberos {

class Texture2D : public Texture
{
public:
	// TODO: Only needed for manually setting up the fields, should be deleted later
	Texture2D() = default;

	Texture2D(const TextureSpecification& spec, const Buffer& buffer);
	explicit Texture2D(const std::filesystem::path& filepath);

	~Texture2D() override;

	static constexpr AssetType Type = AssetType::Texture2D;
    AssetType GetType() override
    {
        return Type;
    }

	static Ref<Texture2D> FromBuffer(const TextureSpecification& spec, const Buffer& buffer);
	static Ref<Texture2D> FromFile(const std::filesystem::path& filepath);

	/// ImGui renders into a UNORM swapchain. A color texture's sampling view is
	/// sRGB (correct for PBR), but sampling that view in ImGui would decode the
	/// texels and make previews/icons too dark. This returns a non-sRGB view
	/// when the image has one, otherwise the normal sampling view.
	const vk::raii::ImageView& GetImGuiImageView() const;

	/// True when the image view uses an sRGB format and sampling returns linear
	/// values. Data textures (normal/metallic/roughness/AO) should normally be
	/// linear so the shader reads their values unchanged.
	bool IsSrgb() const;

private:
	void CreateImGuiDisplayView(const vk::raii::Device& device, const vk::Format imageFormat);

	vk::raii::ImageView m_ImGuiImageView = nullptr;
	bool m_HasImGuiImageView = false;
};

}
