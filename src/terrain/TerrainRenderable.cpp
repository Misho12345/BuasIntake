#include "pch.hpp"
#include "TerrainRenderable.hpp"

#include "gfx/Projection.hpp"

namespace game::terrain
{
	TerrainRenderable::TerrainRenderable() : shader_{
		gfx::Shader::from_graphics_files(
			"assets/shaders/terrain_mesh.vert",
			"assets/shaders/terrain_mesh.frag")
	} {}

	void TerrainRenderable::draw(const gfx::Mesh& mesh, const sf::View& view) const
	{
		if (mesh.empty() || !shader_.valid()) return;

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		shader_.use();
		shader_.set_uniform("uProjection", gfx::make_projection(view));
		mesh.draw();

		glUseProgram(0);
	}
}
