#include "pch.hpp"

#include "render/InstancedSpriteBatch.hpp"

#include "gfx/Projection.hpp"

namespace game::render
{
    // i'm using this and not sf::Vertex because the instanced sprites don't need color
    namespace
    {
        struct QuadVertex final
        {
            vec2 position{ 0.0f, 0.0f };
            vec2 uv{ 0.0f, 0.0f };
        };

        constexpr std::array quad_vertices{
            QuadVertex{ .position = { -0.5f, 0.0f }, .uv = { 0.0f, 1.0f } },
            QuadVertex{ .position = { 0.5f, 0.0f }, .uv = { 1.0f, 1.0f } },
            QuadVertex{ .position = { -0.5f, -1.0f }, .uv = { 0.0f, 0.0f } },
            QuadVertex{ .position = { 0.5f, -1.0f }, .uv = { 1.0f, 0.0f } }
        };
        // the quad is anchored at the bottom center, so trees and rocks grow outward from the terrain surface instead of rotating around their middle
    }

    Result<void> InstancedSpriteBatch::initialize(
        const std::span<const char* const> texture_paths,
        const std::uint32_t                tile_size_pixels)
    {
        if (valid()) return {};
        if (texture_paths.empty()) return fail("InstancedSpriteBatch requires at least one texture path");

        auto shader = gfx::Shader::from_graphics_files(
            "assets/shaders/render/resource_instances.vert",
            "assets/shaders/render/resource_instances.frag");

        if (!shader) return fail(shader.error());

        std::vector<sf::Image> images;
        images.reserve(texture_paths.size());
        for (const auto* path : texture_paths)
        {
            sf::Image image;
            if (!image.loadFromFile(path)) return fail("Failed to load sprite image '{}'", path);
            images.push_back(std::move(image));
        }

        const auto image_size = images.front().getSize();
        for (const auto& image : images)
        {
            // texture arrays require every layer to match, which is why 32px and 64px vegetation are separate batches
            if (image.getSize() != image_size)
            {
                return fail("Instanced sprite texture sheets must share the same size");
            }
        }

        shader_           = std::move(*shader);
        tile_size_pixels_ = tile_size_pixels;
        vao_.create();
        quad_vbo_.create();
        instance_vbo_.create();
        texture_array_.create_2d_array();

        glNamedBufferData(
            quad_vbo_.id(),
            static_cast<GLsizeiptr>(sizeof(quad_vertices)),
            quad_vertices.data(),
            GL_STATIC_DRAW);

        glVertexArrayVertexBuffer(vao_.id(), 0, quad_vbo_.id(), 0, sizeof(QuadVertex));
        glVertexArrayVertexBuffer(vao_.id(), 1, instance_vbo_.id(), 0, sizeof(SpriteInstance));

        // layout(location = 0) in vec2 aLocalPosition;
        static constexpr GLint quad_pos_size = sizeof(QuadVertex::position) / sizeof(float);
        glEnableVertexArrayAttrib(vao_.id(), 0);
        glVertexArrayAttribFormat(vao_.id(), 0, quad_pos_size, GL_FLOAT, GL_FALSE, offsetof(QuadVertex, position));
        glVertexArrayAttribBinding(vao_.id(), 0, 0);

        // layout(location = 1) in vec2 aLocalUv;
        static constexpr GLint quad_uv_size = sizeof(QuadVertex::uv) / sizeof(float);
        glEnableVertexArrayAttrib(vao_.id(), 1);
        glVertexArrayAttribFormat(vao_.id(), 1, quad_uv_size, GL_FLOAT, GL_FALSE, offsetof(QuadVertex, uv));
        glVertexArrayAttribBinding(vao_.id(), 1, 0);

        // layout(location = 2) in vec2 aInstanceCenter;
        static constexpr GLint sprite_center_size = sizeof(SpriteInstance::center_world) / sizeof(float);
        glEnableVertexArrayAttrib(vao_.id(), 2);
        glVertexArrayAttribFormat(vao_.id(), 2, sprite_center_size, GL_FLOAT, GL_FALSE, offsetof(SpriteInstance, center_world));
        glVertexArrayAttribBinding(vao_.id(), 2, 1);

        // layout(location = 3) in vec2 aInstanceUp;
        static constexpr GLint sprite_up_size = sizeof(SpriteInstance::up) / sizeof(float);
        glEnableVertexArrayAttrib(vao_.id(), 3);
        glVertexArrayAttribFormat(vao_.id(), 3, sprite_up_size, GL_FLOAT, GL_FALSE, offsetof(SpriteInstance, up));
        glVertexArrayAttribBinding(vao_.id(), 3, 1);

        // layout(location = 4) in vec4 aInstanceParams0;
        // this packs world_height, radial_offset, texture_layer, and tile_column into one attribute to keep the instance layout compact
        static constexpr GLint sprite_params0_size =
            (offsetof(SpriteInstance, tile_row) - offsetof(SpriteInstance, world_height)) / sizeof(float);
        glEnableVertexArrayAttrib(vao_.id(), 4);
        glVertexArrayAttribFormat(vao_.id(), 4, sprite_params0_size, GL_FLOAT, GL_FALSE, offsetof(SpriteInstance, world_height));
        glVertexArrayAttribBinding(vao_.id(), 4, 1);

        // layout(location = 5) in float aInstanceTileRow;
        static constexpr GLint sprite_tile_row_size = sizeof(SpriteInstance::tile_row) / sizeof(float);
        glEnableVertexArrayAttrib(vao_.id(), 5);
        glVertexArrayAttribFormat(vao_.id(), 5, sprite_tile_row_size, GL_FLOAT, GL_FALSE, offsetof(SpriteInstance, tile_row));
        glVertexArrayAttribBinding(vao_.id(), 5, 1);

        // layout(location = 6) in float aInstanceAngleOffset;
        static constexpr GLint sprite_angle_offset_size = sizeof(SpriteInstance::angle_offset) / sizeof(float);
        glEnableVertexArrayAttrib(vao_.id(), 6);
        glVertexArrayAttribFormat(vao_.id(), 6, sprite_angle_offset_size, GL_FLOAT, GL_FALSE, offsetof(SpriteInstance, angle_offset));
        glVertexArrayAttribBinding(vao_.id(), 6, 1);

        glVertexArrayBindingDivisor(vao_.id(), 1, 1);

        // one array layer per sprite sheet, then the shader picks tile column/row inside that layer per instance
        glTextureStorage3D(
            texture_array_.id(),
            1,
            GL_RGBA8,
            static_cast<GLsizei>(image_size.x),
            static_cast<GLsizei>(image_size.y),
            static_cast<GLsizei>(images.size()));

        for (std::size_t layer = 0; layer < images.size(); ++layer)
        {
            glTextureSubImage3D(
                texture_array_.id(),
                0,
                0,
                0,
                static_cast<GLint>(layer),
                static_cast<GLsizei>(image_size.x),
                static_cast<GLsizei>(image_size.y),
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                images[layer].getPixelsPtr());
        }

        glTextureParameteri(texture_array_.id(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(texture_array_.id(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(texture_array_.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture_array_.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return {};
    }

    void InstancedSpriteBatch::destroy_graphics_resources()
    {
        texture_array_.reset();
        instance_vbo_.reset();
        quad_vbo_.reset();
        vao_.reset();

        shader_            = {};
        instance_count_    = 0;
        instance_capacity_ = 0u;
        tile_size_pixels_  = 32u;
    }

    void InstancedSpriteBatch::upload_instances(const std::span<const SpriteInstance> instances)
    {
        if (!instance_vbo_.valid()) return;

        if (instances.empty())
        {
            instance_count_ = 0;
            return;
        }

        if (instances.size() > instance_capacity_)
        {
            // grow the buffer only when needed, otherwise reuse it to avoid reallocating during camera movement
            glNamedBufferData(
                instance_vbo_.id(),
                static_cast<GLsizeiptr>(instances.size() * sizeof(SpriteInstance)),
                instances.data(),
                GL_DYNAMIC_DRAW);

            instance_capacity_ = instances.size();
        }
        else
        {
            glNamedBufferSubData(
                instance_vbo_.id(),
                0,
                static_cast<GLsizeiptr>(instances.size() * sizeof(SpriteInstance)),
                instances.data());
        }

        instance_count_ = static_cast<GLsizei>(instances.size());
    }

    void InstancedSpriteBatch::draw(const sf::View& view) const
    {
        if (instance_count_ == 0 || !shader_.valid() || !vao_.valid() || !texture_array_.valid()) return;

        if (const auto result = shader_.use(); !result)
        {
            Log::error(result.error());
            return;
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture_array_.id());
        shader_.set_uniform("uProjection", gfx::make_projection(view));
        shader_.set_uniform("uSpriteTextureArray", 0);
        shader_.set_uniform("uTileSizePixels", static_cast<std::int32_t>(tile_size_pixels_));

        glBindVertexArray(vao_.id());
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, instance_count_);
        glBindVertexArray(0);

        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glActiveTexture(GL_TEXTURE0);
    }

    bool InstancedSpriteBatch::valid() const
    {
        return vao_.valid() &&
                quad_vbo_.valid() &&
                instance_vbo_.valid() &&
                texture_array_.valid() &&
                shader_.valid();
    }
}
