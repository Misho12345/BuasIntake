#pragma once

#include "pch.hpp"


#include "ChunkSettings.hpp"
#include "terrain/TerrainFieldSample.hpp"
#include "gfx/SSBO.hpp"
#include "gfx/Shader.hpp"
#include "gfx/Texture2D.hpp"

namespace game::terrain
{
    struct GroundBrushBlocker final
    {
        // a temporary no-dig area, used so terrain edits do not carve through things that must keep support this frame
        vec2  center{ 0.0f, 0.0f };
        vec2  right{ 1.0f, 0.0f };
        vec2  up{ 0.0f, 1.0f };
        vec2  half_extents{ 0.0f, 0.0f };
        float radius{ 0.0f };
    };

    class TerrainGenerator final
    {
    public:
        using FieldSample = TerrainFieldSample;

        static constexpr std::uint32_t terrain_channel_index = 0u;
        static constexpr std::uint32_t water_channel_index   = 1u;

        struct TerrainEdit final
        {
            // packed this way because the brush shader-style math and cpu edit code both want position radius and strength together
            vec4 position_radius_strength{};
            float falloff_exponent{ 1.8f };

            static TerrainEdit make(
                const vec2  world_position,
                const float radius,
                const float signed_strength,
                const float falloff_exponent = 1.8f)
            {
                return {
                    .position_radius_strength = { world_position.x, world_position.y, radius, signed_strength },
                    .falloff_exponent         = falloff_exponent
                };
            }
        };

        struct BoundaryEdge final
        {
            std::uint32_t a{};
            std::uint32_t b{};
        };

        struct RawPipelineResult final
        {
            // raw gpu output before TerrainContour simplifies it for physics and chunk storage
            std::vector<vec2>          boundary_vertices{};
            std::vector<BoundaryEdge>  boundary_edges{};
            std::vector<vec2>          mesh_vertices{};
            std::vector<std::uint32_t> mesh_indices{};
        };

        TerrainGenerator() = default;
        ~TerrainGenerator();

        TerrainGenerator(const TerrainGenerator&)            = delete;
        TerrainGenerator& operator=(const TerrainGenerator&) = delete;
        TerrainGenerator(TerrainGenerator&& other) noexcept;
        TerrainGenerator& operator=(TerrainGenerator&& other) noexcept;

        // initialize builds the gpu side buffers and shaders once for a chunk generator instance
        Result<void> initialize(const ChunkSettings& settings);

        // dispatch runs the full generation pipeline: base terrain, caves, ponds, then terrain contour extraction
        Result<void> dispatch();

        Result<void> dispatch_surface_rebuild(std::uint32_t channel_index, float iso);

        // upload/read_field are used after cpu-side edits so the gpu contour pass can rebuild from the current global field
        Result<void> upload_field(std::span<const FieldSample> field_samples);

        Result<std::vector<FieldSample>> read_field() const;

        Result<RawPipelineResult>        readback();

    private:
        struct FieldLayout final
        {
            // padded fields give neighbor samples around chunk edges so contours do not crack between chunks
            uvec2 padded_size{ 0u, 0u };
            vec2  cell_size{ 0.0f, 0.0f };
            vec2  field_origin{ 0.0f, 0.0f };
            ivec2 field_padding{ 0, 0 };
        };

        void         reset_surface_buffers();
        Result<void> replace_completion_fence();
        void         clear_completion_fence();
        Result<void> wait_for_completion();
        FieldLayout  field_layout() const;

        void bind_field_uniforms(const gfx::Shader& shader, const FieldLayout& layout) const;
        void bind_chunk_uniforms(const gfx::Shader& shader, const FieldLayout& layout) const;
        void bind_generation_pass(const gfx::Shader& shader, const FieldLayout& layout) const;
        void bind_cave_pass(const gfx::Shader& shader, const FieldLayout& layout) const;
        void bind_pond_pass(const gfx::Shader& shader, const FieldLayout& layout) const;

        void bind_surface_edge_pass(
            const gfx::Shader& shader,
            const FieldLayout& layout,
            std::uint32_t      channel_index,
            float              iso) const;

        void bind_surface_mesh_pass(
            const gfx::Shader& shader,
            const FieldLayout& layout,
            std::uint32_t      channel_index,
            float              iso) const;

        Result<RawPipelineResult> consume_readback();

        ChunkSettings settings_{};

        // compute shaders used for the generation of the terrain
        gfx::Shader terrain_shader_{};
        gfx::Shader cave_shader_{};
        gfx::Shader pond_shader_{};
        gfx::Shader edge_shader_{};
        gfx::Shader mesh_shader_{};

        // rgba32f stores terrain, water, wetness, and greenness in one gpu image so all generation passes share the same field
        gfx::Texture2D field_texture_{};

        // storage buffers used by the compute shaders
        gfx::SSBO boundary_vertices_buffer_{};
        gfx::SSBO horizontal_edge_ids_buffer_{};
        gfx::SSBO vertical_edge_ids_buffer_{};
        gfx::SSBO boundary_vertex_counter_buffer_{};
        gfx::SSBO mesh_vertices_buffer_{};
        gfx::SSBO mesh_indices_buffer_{};
        gfx::SSBO boundary_edges_buffer_{};
        gfx::SSBO counters_buffer_{};

        GLsync completion_fence_{ nullptr };
        bool   pending_readback_{ false };
    };
}
