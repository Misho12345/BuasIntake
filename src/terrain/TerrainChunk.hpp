#pragma once

#include "pch.hpp"


#include "ChunkSettings.hpp"
#include "TerrainCollider.hpp"
#include "TerrainContour.hpp"
#include "TerrainGenerator.hpp"
#include "TerrainRenderable.hpp"
#include "gfx/Mesh.hpp"
#include "water/WaterRenderable.hpp"

namespace game::terrain
{
    class TerrainChunk final
    {
      public:
        using FieldSample = TerrainGenerator::FieldSample;

        explicit TerrainChunk(b2WorldId world_id, const ChunkSettings& settings = {});
        ~TerrainChunk() = default;

        TerrainChunk(const TerrainChunk&) = delete;
        TerrainChunk& operator=(const TerrainChunk&) = delete;
        TerrainChunk(TerrainChunk&&) noexcept = default;
        TerrainChunk& operator=(TerrainChunk&&) noexcept = default;

        Result<void> initialize();
        void draw_gl(const sf::View& view) const;
        void draw_water_gl(const sf::View& view) const;
        Result<void> dispatch_generation();
        Result<void> finalize_generation();
        Result<void> rebuild_from_field(std::span<const FieldSample> field_samples,
                                        bool smooth_water = false,
                                        bool rebuild_water = true);
        Result<std::vector<FieldSample>> readback_field() const;

        ivec2 chunk_coord() const;
        vec2 display_min() const;
        vec2 display_max() const;

      private:
        Result<TerrainContour::ScoredResult> read_scored_surface();
        Result<TerrainContour::ScoredResult> rebuild_scored_surface(std::uint32_t channel_index, float iso);
        Result<void> rebuild_chunk_meshes(std::span<const FieldSample> field_samples, bool rebuild_water = true);

        void build_chunk(const TerrainContour::ScoredResult& terrain_result,
                         const TerrainContour::ScoredResult& water_result,
                         std::span<const FieldSample> field_samples);
        void build_terrain_mesh(const std::vector<vec2>& vertices,
                                const std::vector<std::uint32_t>& indices,
                                std::span<const FieldSample> field_samples);
        void build_water_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices);

        ChunkSettings settings_{};

        TerrainGenerator generator_;
        TerrainCollider collider_;
        TerrainRenderable renderable_{};
        water::WaterRenderable water_renderable_{};

        gfx::Mesh mesh_{};
        gfx::Mesh water_mesh_{};

        vec2 display_min_{};
        vec2 display_max_{};
        bool generation_dispatched_{false};
        bool generation_finalized_{false};
    };
}
