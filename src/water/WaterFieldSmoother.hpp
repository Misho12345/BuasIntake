#pragma once

#include "pch.hpp"

#include "gfx/Shader.hpp"
#include "gfx/Texture2D.hpp"

namespace game::water
{
    class WaterFieldSmoother final
    {
    public:
        Result<void> initialize(uvec2 field_size);
        Result<void> smooth(gfx::Texture2D& field_texture, std::uint32_t iterations = 2u) const;

    private:
        void bind_pass(
            const gfx::Texture2D& input_texture,
            const gfx::Texture2D& output_texture) const;

        gfx::Shader    shader_{};
        gfx::Texture2D scratch_texture_{};
    };
}
