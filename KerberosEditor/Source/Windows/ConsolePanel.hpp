#pragma once

#include "EditorWindow.hpp"

namespace Kerberos
{
	class ConsolePanel : public EditorWindow
	{
	public:
		ConsolePanel() = default;
		~ConsolePanel() override = default;

		void OnImGuiRender() override;
		void OnEvent(Event& event) override;

	private:
		bool m_AutoScroll = true;
	};
}