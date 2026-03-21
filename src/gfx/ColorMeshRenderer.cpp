#include "pch.hpp"
#include "ColorMeshRenderer.hpp"

namespace game::gfx
{
	ColorMeshRenderer::ColorMeshRenderer()
	{
		shader_ = Shader::from_graphics_files(
			"assets/shaders/terrain_mesh.vert",
			"assets/shaders/terrain_mesh.frag");
	}

	void ColorMeshRenderer::draw(const Mesh& mesh, const mat4& projection) const
	{
		if (mesh.empty()) return;

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		shader_.use();
		shader_.set_uniform("uProjection", projection);
		mesh.draw();

		glUseProgram(0);
	}
}
