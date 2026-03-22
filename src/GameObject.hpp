#pragma once

#include "pch.hpp"

#include "gfx/Mesh.hpp"
#include "terrain/TerrainRenderable.hpp"
#include "water/WaterRenderable.hpp"

namespace game
{
	class GameObject final
	{
	public:
		using SfDrawable = std::unique_ptr<sf::Drawable>;

		struct GlRenderable final
		{
			const gfx::Mesh* mesh{ nullptr };
			std::variant<
				terrain::TerrainRenderable*,
				water::WaterRenderable*
			> renderer{};
		};

		using Renderable = std::variant<std::monostate, SfDrawable, GlRenderable>;

		GameObject() = default;
		~GameObject() = default;

		GameObject(const GameObject&) = delete;
		GameObject& operator=(const GameObject&) = delete;
		GameObject(GameObject&&) noexcept = default;
		GameObject& operator=(GameObject&&) noexcept = default;

		b2BodyId body{ b2_nullBodyId };
		sf::Transformable transformable{};
		Renderable renderable{};
		sf::RenderStates sfml_states{ sf::RenderStates::Default };

		void sync_from_physics()
		{
			if (!b2Body_IsValid(body)) return;

			const auto [x, y] = b2Body_GetPosition(body);
			const auto angle  = b2Rot_GetAngle(b2Body_GetRotation(body));
			transformable.setPosition({ x, y });
			transformable.setRotation(sf::radians(angle));
		}

		void draw_gl(const sf::View& view) const
		{
			if (const auto* gl = std::get_if<GlRenderable>(&renderable))
			{
				if (!gl->mesh) return;

				std::visit([&](auto* renderer)
				{
					if (renderer) renderer->draw(*gl->mesh, view);
				}, gl->renderer);
			}
		}

		void draw_sf(sf::RenderTarget& target) const
		{
			if (const auto* sf_drawable = std::get_if<SfDrawable>(&renderable))
			{
				if (!*sf_drawable) return;

				auto states = sfml_states;
				states.transform = transformable.getTransform() * sfml_states.transform;
				target.draw(**sf_drawable, states);
			}
		}

		template <typename T, typename... Args> requires std::derived_from<T, sf::Drawable>
		[[nodiscard]]
		static GameObject make_sf(Args&&... args)
		{
			GameObject obj;
			obj.renderable = std::make_unique<T>(std::forward<Args>(args)...);
			return obj;
		}

		template <typename RendererT> requires (
			std::same_as<RendererT, terrain::TerrainRenderable> ||
			std::same_as<RendererT, water::WaterRenderable>)
		[[nodiscard]]
		static GameObject make_gl(const gfx::Mesh& mesh, RendererT& renderer)
		{
			GameObject obj;
			obj.renderable = GlRenderable{ &mesh, &renderer };
			return obj;
		}
	};
}
