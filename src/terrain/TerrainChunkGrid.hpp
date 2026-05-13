#pragma once

#include "pch.hpp"

#include "terrain/TerrainChunk.hpp"
#include "terrain/TerrainField.hpp"

namespace game::terrain
{
    // owns the fixed grid of terrain chunks and translates between chunk-local fields and the global TerrainField
    // PlanetTerrain uses this as the rebuild boundary for meshes physics shapes and water surfaces
    class TerrainChunkGrid final
    {
    public:
        using FieldSample = TerrainFieldSample;

        Result<void> initialize(b2WorldId world_id, const ChunkSettings& settings);

        void draw_gl(const sf::View& view) const;
        void draw_water_gl(const sf::View& view) const;

        void reset_field_layout(TerrainField& field) const;
        Result<void> sync_generated_field_to_global(TerrainField& field) const;

        Result<void> rebuild_dirty_chunks(
            TerrainField&               field,
            const std::vector<bool>&     dirty_chunks,
            bool                         rebuild_water            = true,
            bool                         rebuild_terrain_geometry = true,
            bool                         refresh_terrain_visuals  = true);

        void mark_chunks_covering_global_sample(ivec2 coord, std::vector<bool>& dirty_chunks) const;

        std::vector<bool> make_dirty_chunk_flags(bool dirty = false) const;
        std::size_t       chunk_count() const;

        vec2                 grid_min() const;
        vec2                 grid_max() const;
        vec2                 display_min() const;
        vec2                 display_max() const;

    private:
        static ivec2       chunk_grid_size();
        static std::size_t flat_index(ivec2 chunk_index, ivec2 chunk_count);

        void sync_chunk_field_to_global(
            TerrainField&                field,
            ivec2                        chunk_coord,
            std::span<const FieldSample> field_samples) const;

        std::vector<FieldSample> extract_chunk_field(const TerrainField& field, ivec2 chunk_coord) const;

        ChunkSettings             settings_{};
        std::vector<TerrainChunk> chunks_{};
        vec2                      grid_min_{};
        vec2                      grid_max_{};
        vec2                      display_min_{};
        vec2                      display_max_{};
    };
}
