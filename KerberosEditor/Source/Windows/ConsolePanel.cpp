#include "ConsolePanel.hpp"

#include <imgui/imgui.h>
#include  <spdlog/spdlog.h>

#include <mutex>

import Kerberos;

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

		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_None);

		{
			std::scoped_lock lock(Log::GetEditorSink()->GetMutex());

			const auto& messages = Log::GetEditorSink()->Messages;
			for (size_t i = 0; i < messages.size(); ++i)
			{
				const auto& [Level, Text] = messages[i];

				ImGui::PushID(static_cast<int>(i));

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
				if (ImGui::Selectable(Text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) 
				{
					ImGui::SetClipboardText(Text.c_str());
				}
				ImGui::PopStyleColor();

				ImGui::Separator();
				ImGui::PopID();
			}
		}

		if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) 
		{
			ImGui::SetScrollHereY(1.0f);
		}

		ImGui::EndChild();
		ImGui::End();
	}

	void ConsolePanel::OnEvent(Event& event) 
	{
	}
}
