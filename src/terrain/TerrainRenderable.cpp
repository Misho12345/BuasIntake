#include "pch.hpp"
#include "TerrainRenderable.hpp"

#include "gfx/AlphaBlendPass.hpp"
#include "gfx/Projection.hpp"
#include "terrain/TerrainConstants.hpp"

namespace game::terrain
{
	namespace
	{
		constexpr float rock_blend_start_depth = 0.12f;
		constexpr float rock_blend_end_depth = 0.52f;

		Result<void> load_terrain_texture(sf::Texture& texture, const char* path)
		{
			if (!texture.loadFromFile(path))
			{
				return fail("Failed to load terrain texture '{}'", path);
			}

			texture.setRepeated(true);
			texture.setSmooth(true);
			return {};
		}
	}

	Result<void> TerrainRenderable::initialize()
	{
		auto shader = gfx::Shader::from_graphics_files(
			"assets/shaders/default.vert",
			"assets/shaders/terrain_mesh.frag");

		if (!shader) return fail(shader.error());
		shader_ = std::move(*shader);

		TRY(load_terrain_texture(dirt_texture_, "assets/images/textures/dirt.png"));
		TRY(load_terrain_texture(grass_texture_, "assets/images/textures/grass.png"));
		TRY(load_terrain_texture(rock_texture_, "assets/images/textures/rock.png"));
		TRY(load_terrain_texture(hard_rock_texture_, "assets/images/textures/hard_rock.png"));

		return {};
	}

	void TerrainRenderable::draw(const gfx::Mesh& mesh, const sf::View& view) const
	{
		if (mesh.empty() || !shader_.valid()) return;

		const gfx::ScopedAlphaBlendPass blend_pass{};
		static_cast<void>(blend_pass);

		//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		if (const auto use_result = shader_.use(); !use_result)
		{
			Log::error(use_result.error());
			return;
		}

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, dirt_texture_.getNativeHandle());

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, grass_texture_.getNativeHandle());

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, rock_texture_.getNativeHandle());

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, hard_rock_texture_.getNativeHandle());

		shader_.set_uniform("uProjection", gfx::make_projection(view));
		shader_.set_uniform("uDirtTexture", 0);
		shader_.set_uniform("uGrassTexture", 1);
		shader_.set_uniform("uRockTexture", 2);
		shader_.set_uniform("uHardRockTexture", 3);
		shader_.set_uniform("uTextureScale", 0.085f);
		shader_.set_uniform("uRockBlendStartDepth", rock_blend_start_depth);
		shader_.set_uniform("uRockBlendEndDepth", rock_blend_end_depth);
		shader_.set_uniform("uHardRockStartDepth", constants::hard_rock_depth_threshold);

		mesh.draw();

		//glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, 0);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}
