#include "pch.hpp"
#include "TerrainRenderable.hpp"

#include "gfx/Projection.hpp"

namespace game::terrain
{
	TerrainRenderable::TerrainRenderable() : shader_{
		gfx::Shader::from_graphics_files(
			"assets/shaders/default.vert",
			"assets/shaders/terrain_mesh.frag")
	}
	{
		if (!dirt_texture_.loadFromFile("assets/images/dirt.png"))
		{
			std::println(std::cerr, "Failed to load assets/images/dirt.png");
		}
		else
		{
			dirt_texture_.setRepeated(true);
			dirt_texture_.setSmooth(true);
		}
	}

	void TerrainRenderable::draw(const gfx::Mesh& mesh, const sf::View& view) const
	{
		if (mesh.empty() || !shader_.valid()) return;

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		shader_.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, dirt_texture_.getNativeHandle());
		shader_.set_uniform("uProjection", gfx::make_projection(view));
		shader_.set_uniform("uDirtTexture", static_cast<std::int32_t>(0));
		shader_.set_uniform("uTextureScale", 0.085f);
		mesh.draw();

		//glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		glBindTexture(GL_TEXTURE_2D, 0);
		glUseProgram(0);
	}
}
