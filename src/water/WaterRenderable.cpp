#include "pch.hpp"
#include "WaterRenderable.hpp"

#include "gfx/Projection.hpp"

namespace game::water
{
	WaterRenderable::WaterRenderable() : shader_{
		gfx::Shader::from_graphics_files(
			"assets/shaders/default.vert",
			"assets/shaders/water_mesh.frag")
	} {}

	void WaterRenderable::draw(const gfx::Mesh& mesh, const sf::View& view) const
	{
		if (mesh.empty() || !shader_.valid()) return;

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		shader_.use();
		shader_.set_uniform("uProjection", gfx::make_projection(view));
		shader_.set_uniform("uTime", animation_clock_.getElapsedTime().asSeconds());
		mesh.draw();

		//glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		glUseProgram(0);
	}
}
