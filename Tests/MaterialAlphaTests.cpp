#include "Renderer/Material.hpp"

#include <gtest/gtest.h>

namespace Kerberos
{
	TEST(MaterialAlphaTest, DefaultsToOpaque)
	{
		const Material material;
		EXPECT_EQ(material.AlphaMode, AlphaMode::Opaque);
		EXPECT_FALSE(material.IsTransparent());
		EXPECT_FALSE(material.IsMasked());
	}

	TEST(MaterialAlphaTest, BlendModeUsesTransparentPass)
	{
		Material material;
		material.AlphaMode = AlphaMode::Blend;
		EXPECT_TRUE(material.IsTransparent());
		EXPECT_FALSE(material.IsMasked());
	}

	TEST(MaterialAlphaTest, MaskedModeStaysOpaque)
	{
		Material material;
		material.AlphaMode = AlphaMode::Mask;
		EXPECT_FALSE(material.IsTransparent());
		EXPECT_TRUE(material.IsMasked());
	}

	TEST(MaterialAlphaTest, LegacyAlphaBelowOneBecomesBlend)
	{
		const Material material("Glass", glm::vec4(1.0f, 1.0f, 1.0f, 0.4f), 0.1f, 0.0f);
		EXPECT_EQ(material.AlphaMode, AlphaMode::Blend);
		EXPECT_TRUE(material.IsTransparent());
	}
}
