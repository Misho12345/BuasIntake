#pragma once

#include "pch.hpp"

#include "gfx/Mesh.hpp"
#include "gfx/Shader.hpp"

namespace game::water
{
	struct WaterRenderable
	{
		WaterRenderable();
		~WaterRenderable() = default;

		WaterRenderable(const WaterRenderable&) = delete;
		WaterRenderable& operator=(const WaterRenderable&) = delete;
		WaterRenderable(WaterRenderable&&) noexcept = default;
		WaterRenderable& operator=(WaterRenderable&&) noexcept = default;

		void draw(const gfx::Mesh& mesh, const sf::View& view) const;

		float time{ 0.0f };
		sf::Texture* texture{ nullptr };

	private:
		gfx::Shader shader_{};
	};
}
