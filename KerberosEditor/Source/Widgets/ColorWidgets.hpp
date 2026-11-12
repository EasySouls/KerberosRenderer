#pragma once

#include <imgui.h>
#include <glm/glm.hpp>

namespace Kerberos::Editor::ColorWidgets
{
	/// All editor-authored colors are sRGB. 
	/// The underlying values stay normalized floats and are converted to linear at
	/// the renderer's GPU upload boundary.
	inline bool SrgbColorEdit3(const char* label, glm::vec3& color)
	{
		const bool changed = ImGui::ColorEdit3(label, &color[0], ImGuiColorEditFlags_Uint8);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("Authored in sRGB. Stored as normalized values and converted to linear for rendering.");
		return changed;
	}

	inline bool SrgbColorEdit4(const char* label, glm::vec4& color)
	{
		const bool changed = ImGui::ColorEdit4(label, &color[0], ImGuiColorEditFlags_Uint8);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("Authored in sRGB. RGB is converted to linear for rendering. Alpha is coverage.");
		return changed;
	}
}
