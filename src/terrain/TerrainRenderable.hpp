#pragma once

#include "pch.hpp"

#include "gfx/Mesh.hpp"
#include "gfx/Shader.hpp"

namespace game::terrain
{
	struct TerrainRenderable
	{
		TerrainRenderable();
		~TerrainRenderable() = default;

		TerrainRenderable(const TerrainRenderable&) = delete;
		TerrainRenderable& operator=(const TerrainRenderable&) = delete;
		TerrainRenderable(TerrainRenderable&&) noexcept = default;
		TerrainRenderable& operator=(TerrainRenderable&&) noexcept = default;

		void draw(const gfx::Mesh& mesh, const sf::View& view) const;

	private:
		gfx::Shader shader_{};
		sf::Texture dirt_texture_{};
	};
}
