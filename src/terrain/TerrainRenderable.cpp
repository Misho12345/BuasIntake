#include "pch.hpp"
#include "TerrainRenderable.hpp"

#include "gfx/Projection.hpp"

namespace game::terrain
{
	namespace
	{
		constexpr float rock_blend_start_depth = 0.12f;
		constexpr float rock_blend_end_depth = 0.52f;
		constexpr float hard_rock_start_depth = 0.60f;
	}

	TerrainRenderable::TerrainRenderable() : shader_{
		gfx::Shader::from_graphics_files(
			"assets/shaders/default.vert",
			"assets/shaders/terrain_mesh.frag")
	}
	{
		auto load_texture = [](sf::Texture& texture, const char* path)
		{
			if (!texture.loadFromFile(path))
			{
				std::println(std::cerr, "Failed to load {}", path);
				return;
			}

			texture.setRepeated(true);
			texture.setSmooth(true);
		};

		load_texture(dirt_texture_, "assets/images/textures/dirt.png");
		load_texture(rock_texture_, "assets/images/textures/rock.png");
		load_texture(hard_rock_texture_, "assets/images/textures/hard_rock.png");
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
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, rock_texture_.getNativeHandle());
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, hard_rock_texture_.getNativeHandle());
		shader_.set_uniform("uProjection", gfx::make_projection(view));
		shader_.set_uniform("uDirtTexture", static_cast<std::int32_t>(0));
		shader_.set_uniform("uRockTexture", static_cast<std::int32_t>(1));
		shader_.set_uniform("uHardRockTexture", static_cast<std::int32_t>(2));
		shader_.set_uniform("uTextureScale", 0.085f);
		shader_.set_uniform("uRockBlendStartDepth", rock_blend_start_depth);
		shader_.set_uniform("uRockBlendEndDepth", rock_blend_end_depth);
		shader_.set_uniform("uHardRockStartDepth", hard_rock_start_depth);
		mesh.draw();

		//glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glUseProgram(0);
	}
}
