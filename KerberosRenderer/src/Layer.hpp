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

		std::string GetName() const { return name; }

		virtual void OnUpdate(float deltaTime) = 0;
		virtual void OnEvent() = 0;
		virtual void OnImGuiRender() = 0;

	private:
		std::string name;
	};
}