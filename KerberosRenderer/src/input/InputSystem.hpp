#pragma once

#include "KeyCodes.hpp"
#include "MouseButtonCodes.hpp"

#include "glm/glm.hpp"

namespace kbr
{
	class Input
	{
	public:
		static bool IsKeyPressed(KeyCode keycode);
		static bool IsMouseButtonPressed(MouseButtonCode button);
		static glm::vec2 GetMousePosition();
		static float GetMouseX();
		static float GetMouseY();

		Input() = delete;
	};
}