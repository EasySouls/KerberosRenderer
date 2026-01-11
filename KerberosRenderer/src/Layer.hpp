#pragma once

#include <string>
#include <utility>

namespace kbr 
{
	class Layer
	{
	public:
		explicit Layer(std::string name)
			: name(std::move(name))
		{}

		virtual ~Layer() = default;

		virtual void OnAttach() = 0;
		virtual void OnDetach() = 0;

		virtual void OnUpdate(float deltaTime) = 0;
		virtual void OnEvent() = 0;
		virtual void OnImGuiRender() = 0;

		std::string GetName() const { return name; }

	private:
		std::string name;
	};
}