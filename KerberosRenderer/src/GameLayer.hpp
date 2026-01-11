#pragma once

#include "Layer.hpp"

namespace Game
{

	class GameLayer : public kbr::Layer
	{
	public:
		GameLayer()
			: Layer("GameLayer")
		{}

		void OnAttach() override;
		void OnDetach() override;

		void OnUpdate(float deltaTime) override;
		void OnEvent() override;
		void OnImGuiRender() override;
	};

}