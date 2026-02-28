#pragma once

#include "Events/Event.hpp"
#include "Core/Core.hpp"

#include <memory>
#include <string>
#include <utility>

namespace Kerberos 
{
	class Layer
	{
	public:
		explicit Layer(std::string name)
			: m_Name(std::move(name))
		{}

		virtual ~Layer() = default;

		virtual void OnAttach() = 0;
		virtual void OnDetach() = 0;

		virtual void OnUpdate(float deltaTime) = 0;
		virtual void OnEvent(const Ref<Event>& event) = 0;
		virtual void OnImGuiRender() = 0;

		std::string GetName() const { return m_Name; }

	private:
		std::string m_Name;
	};
}
