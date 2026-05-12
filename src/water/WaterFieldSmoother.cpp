#include "pch.hpp"

#include "water/WaterFieldSmoother.hpp"

#include "gfx/ComputeDispatcher.hpp"

namespace game::water
{
    Result<void> WaterFieldSmoother::initialize(const uvec2 field_size)
    {
        auto shader = gfx::Shader::from_compute_file("assets/shaders/water/water_smooth.comp");
        if (!shader) return fail(shader.error());
        shader_ = std::move(*shader);

        return scratch_texture_.create_rgba32f(field_size);
    }

    Result<void> WaterFieldSmoother::smooth(gfx::Texture2D& field_texture, const std::uint32_t iterations) const
    {
        if (iterations == 0u) return {};

        const auto size   = field_texture.size();
        const auto groups = gfx::ComputeDispatcher::groups_for(size, 16, 16);
        for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            const bool  write_to_scratch = (iteration % 2u) == 0u;
            const auto& input_texture    = write_to_scratch ? field_texture : scratch_texture_;
            const auto& output_texture   = write_to_scratch ? scratch_texture_ : field_texture;

            const std::array smooth_passes{
                gfx::ComputeDispatcher::Pass{
                    .shader    = &shader_,
                    .groups    = groups,
                    .configure = [this, &input_texture, &output_texture](const gfx::Shader&)
                    {
                        bind_pass(input_texture, output_texture);
                    },
                    .barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                }
            };

            if (auto result = gfx::ComputeDispatcher::run(smooth_passes);
                !result)
                return fail(result.error());
        }

        if ((iterations % 2u) != 0u)
        {
            glCopyImageSubData(
                scratch_texture_.native_handle(),
                GL_TEXTURE_2D,
                0,
                0,
                0,
                0,
                field_texture.native_handle(),
                GL_TEXTURE_2D,
                0,
                0,
                0,
                0,
                static_cast<GLsizei>(size.x),
                static_cast<GLsizei>(size.y),
                1);

            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }

        return {};
    }

    void WaterFieldSmoother::bind_pass(
        const gfx::Texture2D& input_texture,
        const gfx::Texture2D& output_texture) const
    {
        input_texture.bind_image(0u, GL_READ_ONLY);
        output_texture.bind_image(1u, GL_WRITE_ONLY);
    }
}
