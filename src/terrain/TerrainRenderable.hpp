#pragma once

#include "pch.hpp"

#include "gfx/Mesh.hpp"
#include "gfx/Shader.hpp"

namespace game::terrain
{
	struct TerrainRenderable
	{
		TerrainRenderable() = default;
		~TerrainRenderable() = default;

		TerrainRenderable(const TerrainRenderable&) = delete;
		TerrainRenderable& operator=(const TerrainRenderable&) = delete;
		TerrainRenderable(TerrainRenderable&&) noexcept = default;
		TerrainRenderable& operator=(TerrainRenderable&&) noexcept = default;

		[[nodiscard]] Result<void> initialize();
		void draw(const gfx::Mesh& mesh, const sf::View& view) const;

	private:
		gfx::Shader shader_{};
		sf::Texture dirt_texture_{};
		sf::Texture grass_texture_{};
		sf::Texture rock_texture_{};
		sf::Texture hard_rock_texture_{};
	};
}
