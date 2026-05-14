#include "pch.hpp"

#include "TerrainRenderable.hpp"

#include "gfx/Projection.hpp"
#include "terrain/TerrainConstants.hpp"

namespace game::terrain
{
    struct TerrainRenderable::SharedAssets final
    {
        gfx::Shader shader{};
        sf::Texture dirt_texture{};
        sf::Texture grass_texture{};
        sf::Texture rock_texture{};
        sf::Texture hard_rock_texture{};
    };

    namespace
    {
        std::weak_ptr<TerrainRenderable::SharedAssets> shared_assets_cache;

        Result<void> load_terrain_texture(sf::Texture& texture, const char* path)
        {
            if (!texture.loadFromFile(path)) return fail("Failed to load terrain texture '{}'", path);

            texture.setRepeated(true);
            texture.setSmooth(true);
            return {};
        }

        Result<std::shared_ptr<TerrainRenderable::SharedAssets>> acquire_shared_assets()
        {
            if (const auto shared_assets = shared_assets_cache.lock();
                shared_assets) { return shared_assets; }

            auto shared_assets = std::make_shared<TerrainRenderable::SharedAssets>();
            auto shader        = gfx::Shader::from_graphics_files(
                "assets/shaders/render/default.vert",
                "assets/shaders/terrain/terrain_mesh.frag");

            if (!shader) return fail(shader.error());
            shared_assets->shader = std::move(*shader);

            TRY(load_terrain_texture(shared_assets->dirt_texture, "assets/images/textures/dirt.png"));
            TRY(load_terrain_texture(shared_assets->grass_texture, "assets/images/textures/grass.png"));
            TRY(load_terrain_texture(shared_assets->rock_texture, "assets/images/textures/rock.png"));
            TRY(load_terrain_texture(shared_assets->hard_rock_texture, "assets/images/textures/hard_rock.png"));

            shared_assets_cache = shared_assets;
            return shared_assets;
        }
    }

    Result<void> TerrainRenderable::initialize()
    {
        auto shared_assets = acquire_shared_assets();
        if (!shared_assets) return fail(shared_assets.error());
        assets_ = std::move(*shared_assets);
        return {};
    }

    void TerrainRenderable::draw(const gfx::Mesh& mesh, const sf::View& view) const
    {
        if (mesh.empty() || assets_ == nullptr || !assets_->shader.valid()) return;

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        if (const auto use_result = assets_->shader.use();
            !use_result)
        {
            Log::error(use_result.error());
            return;
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, assets_->dirt_texture.getNativeHandle());

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, assets_->grass_texture.getNativeHandle());

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, assets_->rock_texture.getNativeHandle());

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, assets_->hard_rock_texture.getNativeHandle());

        assets_->shader.set_uniform("uProjection", gfx::make_projection(view));
        assets_->shader.set_uniform("uDirtTexture", 0);
        assets_->shader.set_uniform("uGrassTexture", 1);
        assets_->shader.set_uniform("uRockTexture", 2);
        assets_->shader.set_uniform("uHardRockTexture", 3);
        assets_->shader.set_uniform("uTextureScale", 0.085f);
        assets_->shader.set_uniform("uRockBlendStartDepth", constants::terrain_rock_blend_start_depth);
        assets_->shader.set_uniform("uRockBlendEndDepth", constants::terrain_rock_blend_end_depth);
        assets_->shader.set_uniform("uHardRockStartDepth", constants::hard_rock_depth_threshold);

        mesh.draw();

        // glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }
}
