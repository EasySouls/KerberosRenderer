#pragma once

namespace Kerberos
{
	class Event;

	class EditorWindow
	{
	public:
		EditorWindow() = default;
		virtual ~EditorWindow() = default;

		EditorWindow(const EditorWindow& other) = default;
		EditorWindow(EditorWindow&& other) noexcept = default;
		EditorWindow& operator=(const EditorWindow& other) = default;
		EditorWindow& operator=(EditorWindow&& other) noexcept = default;

		virtual void OnEvent(Event& event) = 0;
		virtual void OnImGuiRender() = 0;
	};
}