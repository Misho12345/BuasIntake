#pragma once

#include "pch.hpp"

#include "gfx/GlHandle.hpp"
#include "gfx/Shader.hpp"

namespace game::render
{
    struct SpriteInstance final
    {
        vec2  center_world{ 0.0f, 0.0f };
        vec2  up{ 0.0f, -1.0f };
        float world_height{ 1.0f };
        float radial_offset{ 0.0f };
        float texture_layer{ 0.0f };
        float tile_column{ 0.0f };
        float tile_row{ 0.0f };
        float angle_offset{ 0.0f };
    };

    class InstancedSpriteBatch final
    {
    public:
        Result<void> initialize(std::span<const char* const> texture_paths, std::uint32_t tile_size_pixels);
        void destroy_graphics_resources();
        void upload_instances(std::span<const SpriteInstance> instances);
        void draw(const sf::View& view) const;
        bool valid() const;

    private:
        gfx::GlVertexArray vao_{};
        gfx::GlBuffer      quad_vbo_{};
        gfx::GlBuffer      instance_vbo_{};

        gfx::GlTexture texture_array_{};
        GLsizei        instance_count_{ 0 };
        std::size_t    instance_capacity_{ 0u };
        std::uint32_t  tile_size_pixels_{ 32u };

        gfx::Shader shader_{};
    };
}
