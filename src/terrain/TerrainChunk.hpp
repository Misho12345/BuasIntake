#pragma once

#include "pch.hpp"


#include "ChunkSettings.hpp"
#include "TerrainCollider.hpp"
#include "TerrainContour.hpp"
#include "TerrainFieldSample.hpp"
#include "TerrainGenerator.hpp"
#include "TerrainRenderable.hpp"
#include "gfx/Mesh.hpp"
#include "water/WaterChunkSurface.hpp"

namespace game::terrain
{
    // one generated chunk of the planet
    class TerrainChunk final
    {
    public:
        using FieldSample = TerrainFieldSample;

        explicit TerrainChunk(b2WorldId world_id, const ChunkSettings& settings = {});
        ~TerrainChunk() = default;

        TerrainChunk(const TerrainChunk&)                = delete;
        TerrainChunk& operator=(const TerrainChunk&)     = delete;
        TerrainChunk(TerrainChunk&&) noexcept            = default;
        TerrainChunk& operator=(TerrainChunk&&) noexcept = default;

        Result<void> initialize();

        void draw_gl(const sf::View& view) const;
        void draw_water_gl(const sf::View& view) const;

        // generation is split so many chunks can dispatch gpu work first and read back later
        Result<void> dispatch_generation();
        Result<void> finalize_generation();

        Result<void> upload_rebuild_field(std::span<const FieldSample> field_samples);

        void refresh_cached_terrain_mesh(std::span<const FieldSample> field_samples);

        Result<void> dispatch_terrain_surface_rebuild();
        Result<void> finalize_terrain_surface_rebuild(std::span<const FieldSample> field_samples);
        Result<void> dispatch_water_surface_rebuild();
        Result<void> finalize_water_surface_rebuild();

        Result<std::vector<FieldSample>> readback_field() const;

        ivec2 chunk_coord() const;
        vec2  display_min() const;
        vec2  display_max() const;

    private:
        Result<TerrainContour::ScoredResult> read_scored_surface();
        Result<TerrainContour::ScoredResult> rebuild_scored_surface(std::uint32_t channel_index, float iso);

        Result<void> rebuild_chunk_meshes(
            std::span<const FieldSample> field_samples,
            bool                         rebuild_water            = true,
            bool                         rebuild_terrain_geometry = true);

        void build_chunk(
            const TerrainContour::ScoredResult& terrain_result,
            const TerrainContour::ScoredResult& water_result,
            std::span<const FieldSample>        field_samples);
        void cache_terrain_surface(
            const TerrainContour::ScoredResult& terrain_result,
            std::span<const FieldSample>        field_samples);
        void build_terrain_mesh(
            const std::vector<vec2>&          vertices,
            const std::vector<std::uint32_t>& indices,
            std::span<const FieldSample>      field_samples);

        ChunkSettings settings_{};

        TerrainGenerator         generator_;
        TerrainCollider          collider_;
        TerrainRenderable        renderable_{};
        water::WaterChunkSurface water_surface_{};

        gfx::Mesh                  mesh_{};
        std::vector<vec2>          cached_terrain_vertices_{};
        std::vector<std::uint32_t> cached_terrain_indices_{};

        vec2 display_min_{};
        vec2 display_max_{};
        bool generation_dispatched_{ false };
        bool generation_finalized_{ false };
    };
}
