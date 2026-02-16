#include "pch.hpp"
#include "InputSystem.hpp"

#include "../Application.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace kbr
{

	bool Input::IsKeyPressed(const KeyCode keycode)
	{
		const auto window = Application::Get().GetWindow();
		const auto state = glfwGetKey(window, static_cast<int32_t>(keycode));

		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool Input::IsMouseButtonPressed(const MouseButtonCode button)
	{
		const auto window = Application::Get().GetWindow();
		const auto state = glfwGetMouseButton(window, static_cast<int8_t>(button));

		return state == GLFW_PRESS;
	}

	glm::vec2 Input::GetMousePosition()
	{
		const auto window = Application::Get().GetWindow();
		double xPos, yPos;
		glfwGetCursorPos(window, &xPos, &yPos);

		float xPosF = static_cast<float>(xPos);
		float yPosF = static_cast<float>(yPos);

		return { xPosF, yPosF };
	}

	float Input::GetMouseX()
	{
		const auto pos = GetMousePosition();
		return pos.x;
	}

	float Input::GetMouseY()
	{
		const auto pos = GetMousePosition();
		return pos.y;
	}
}
