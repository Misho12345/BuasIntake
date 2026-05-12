#pragma once

#include "pch.hpp"


#include "terrain/TerrainGenerator.hpp"

namespace game::terrain
{
    struct TerrainSurfaceFieldView final
    {
        vec2  world_center{ 0.0f, 0.0f };
        vec2  field_origin{ 0.0f, 0.0f };
        vec2  cell_size{ 1.0f, 1.0f };
        uvec2 field_size{ 0u, 0u };
        float planet_radius{ 1.0f };

        std::span<const TerrainGenerator::FieldSample> field_samples{};
    };

    struct TerrainSurfaceAttachment final
    {
        vec2  anchor_world{ 0.0f, 0.0f };
        vec2  surface_up{ 0.0f, 0.0f };
        float floor_alignment{ 0.0f };
    };

    namespace terrain_surface_sampler
    {
        bool is_surface_exposed_world(
            const TerrainSurfaceFieldView& view,
            vec2                           world_position,
            float                          clearance_distance);

        std::optional<TerrainSurfaceAttachment> exposed_surface_attachment(
            const TerrainSurfaceFieldView& view,
            ivec2                          coord);

        std::optional<vec2> surface_anchor_world(
            const TerrainSurfaceFieldView& view,
            ivec2                          coord);
    }
}
