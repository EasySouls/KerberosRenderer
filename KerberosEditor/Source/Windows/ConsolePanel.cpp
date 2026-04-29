#include "ConsolePanel.hpp"

#include "Logging/Log.hpp"

#include <imgui/imgui.h>

namespace Kerberos
{
	void ConsolePanel::OnImGuiRender()
	{
		ImGui::Begin("Console");
		if (ImGui::Button("Clear"))
		{
			Log::GetEditorSink()->Clear();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
		ImGui::Separator();
		const auto& messages = Log::GetEditorSink()->Messages;
		for (const auto& [Level, Text] : messages)
		{
			ImVec4 color;
			switch (Level)
			{
				case spdlog::level::trace:    color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
				case spdlog::level::debug:    color = ImVec4(0.0f, 1.0f, 1.0f, 1.0f); break;
				case spdlog::level::info:     color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
				case spdlog::level::warn:     color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); break;
				case spdlog::level::err:      color = ImVec4(1.0f, 0.5f, 0.5f, 1.0f); break;
				case spdlog::level::critical: color = ImVec4(1.0f, 0.25f, 0.25f, 1.0f); break;
				default:                     color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextUnformatted(Text.c_str());
			ImGui::PopStyleColor();
		}
		if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);

		ImGui::End();
	}

	void ConsolePanel::OnEvent(Event& event) 
	{
	}
}
