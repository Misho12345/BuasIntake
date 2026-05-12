#include "pch.hpp"

#include "WaterRenderable.hpp"

#include "gfx/AlphaBlendPass.hpp"
#include "gfx/Projection.hpp"

namespace game::water
{
    namespace
    {
        float shared_water_animation_time()
        {
            static sf::Clock animation_clock;
            return animation_clock.getElapsedTime().asSeconds();
        }
    }

    Result<void> WaterRenderable::initialize()
    {
        auto shader = gfx::Shader::from_graphics_files(
            "assets/shaders/render/default.vert",
            "assets/shaders/water/water_mesh.frag");

        if (!shader) return fail(shader.error());

        shader_ = std::move(*shader);
        return {};
    }

    void WaterRenderable::draw(const gfx::Mesh& mesh, const sf::View& view) const
    {
        if (mesh.empty() || !shader_.valid()) return;

        [[maybe_unused]]
        const gfx::ScopedAlphaBlendPass blend_pass{};

        if (const auto use_result = shader_.use();
            !use_result)
        {
            Log::error(use_result.error());
            return;
        }

        shader_.set_uniform("uProjection", gfx::make_projection(view));
        shader_.set_uniform("uTime", shared_water_animation_time());
        mesh.draw();

        glUseProgram(0);
    }
}
