#pragma once

#include "pch.hpp"


namespace game
{
	class GameObject final
	{
	public:
		using Drawable = std::unique_ptr<sf::Drawable>;

		GameObject()  = default;
		~GameObject() = default;

		GameObject(const GameObject&)                = delete;
		GameObject& operator=(const GameObject&)     = delete;
		GameObject(GameObject&&) noexcept            = default;
		GameObject& operator=(GameObject&&) noexcept = default;

		b2BodyId          body{ b2_nullBodyId };
		sf::Transformable transformable{};
		Drawable          renderable{};

		void sync_from_physics()
		{
			if (!b2Body_IsValid(body)) return;

			const auto [x, y] = b2Body_GetPosition(body);
			const auto angle  = b2Rot_GetAngle(b2Body_GetRotation(body));
			transformable.setPosition({ x, y });
			transformable.setRotation(sf::radians(angle));
		}

		void draw_sf(sf::RenderTarget& target) const
		{
			if (!renderable) return;

			sf::RenderStates states{ sf::RenderStates::Default };
			states.transform = transformable.getTransform();
			target.draw(*renderable, states);
		}
	};
}
