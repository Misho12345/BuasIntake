#include "pch.hpp"

#include "terrain/TerrainSurfaceSampler.hpp"

namespace game::terrain
{
    namespace
    {
        bool field_ready(const TerrainSurfaceFieldView& view)
        {
            return !view.field_samples.empty() &&
                    view.field_size.x > 0u &&
                    view.field_size.y > 0u;
        }

        bool is_valid_global_sample(const TerrainSurfaceFieldView& view, const ivec2 coord)
        {
            return coord.x >= 0 && coord.y >= 0 &&
                    coord.x < static_cast<int>(view.field_size.x) &&
                    coord.y < static_cast<int>(view.field_size.y);
        }

        std::size_t global_field_index(const TerrainSurfaceFieldView& view, const ivec2 coord)
        {
            return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(view.field_size.x) +
                    static_cast<std::size_t>(coord.x);
        }

        vec2 global_sample_world_position(const TerrainSurfaceFieldView& view, const ivec2 coord)
        {
            return view.field_origin + view.cell_size * coord;
        }

        ivec2 world_to_global_sample(const TerrainSurfaceFieldView& view, const vec2 world_position)
        {
            const vec2 grid = (world_position - view.field_origin) / view.cell_size;

            return {
                std::clamp(static_cast<int>(std::lround(grid.x)), 0, static_cast<int>(view.field_size.x) - 1),
                std::clamp(static_cast<int>(std::lround(grid.y)), 0, static_cast<int>(view.field_size.y) - 1)
            };
        }

        // we step outward until we leave solid terrain then bisect back because that gives a stable boundary point without needing extra mesh data
        std::optional<vec2> march_surface_anchor(
            const TerrainSurfaceFieldView& view,
            const ivec2                    coord,
            const vec2                     direction,
            const float                    step_size,
            const float                    max_distance)
        {
            const vec2 center     = global_sample_world_position(view, coord);
            vec2       last_solid = center;

            for (float distance = step_size; distance <= max_distance; distance += step_size)
            {
                const vec2 probe       = center + direction * distance;
                const auto probe_coord = world_to_global_sample(view, probe);

                if (!is_valid_global_sample(view, probe_coord) ||
                    !is_solid_sample(view.field_samples[global_field_index(view, probe_coord)]))
                {
                    vec2 low  = last_solid;
                    vec2 high = probe;
                    for (int iteration = 0; iteration < 8; ++iteration)
                    {
                        const vec2 mid       = lerp(low, high, 0.5f);
                        const auto mid_coord = world_to_global_sample(view, mid);

                        if (is_valid_global_sample(view, mid_coord) &&
                            is_solid_sample(view.field_samples[global_field_index(view, mid_coord)]))
                            low = mid;
                        else high = mid;
                    }

                    return lerp(low, high, 0.5f);
                }

                last_solid = probe;
            }

            return std::nullopt;
        }
    }

    std::optional<TerrainSurfaceAttachment> terrain_surface_sampler::exposed_surface_attachment(
        const TerrainSurfaceFieldView& view,
        const ivec2                    coord)
    {
        if (!field_ready(view)) return std::nullopt;
        if (!is_valid_global_sample(view, coord)) return std::nullopt;
        if (!is_solid_sample(view.field_samples[global_field_index(view, coord)])) return std::nullopt;

        const vec2 center    = global_sample_world_position(view, coord);
        const vec2 radial_up = normalize(center - view.world_center);
        vec2       surface_up{ 0.0f, 0.0f };
        int        open_neighbors = 0;

        for (int oy = -1; oy <= 1; ++oy)
        {
            for (int ox = -1; ox <= 1; ++ox)
            {
                if (ox == 0 && oy == 0) continue;

                const ivec2 neighbor = coord + ivec2{ ox, oy };

                if (!is_valid_global_sample(view, neighbor)) continue;
                if (is_solid_sample(view.field_samples[global_field_index(view, neighbor)])) continue;

                surface_up += normalize(global_sample_world_position(view, neighbor) - center, { 0.0f, 0.0f });
                ++open_neighbors;
            }
        }

        if (open_neighbors == 0) return std::nullopt;

        // Use the surrounding open cells to estimate the local outward direction, then march to the actual surface.
        surface_up        = normalize(surface_up, radial_up);
        const auto anchor = march_surface_anchor(
            view,
            coord,
            surface_up,
            std::max(min(view.cell_size) * 0.10f, 0.02f),
            std::max(min(view.cell_size) * 3.4f, 0.45f));

        if (!anchor.has_value()) return std::nullopt;

        return TerrainSurfaceAttachment{
            .anchor_world    = *anchor,
            .surface_up      = surface_up,
            .floor_alignment = std::clamp(surface_up.dot(radial_up), -1.0f, 1.0f)
        };
    }

    std::optional<vec2> terrain_surface_sampler::surface_anchor_world(
        const TerrainSurfaceFieldView& view,
        const ivec2                    coord)
    {
        if (!field_ready(view)) return std::nullopt;
        if (!is_valid_global_sample(view, coord)) return std::nullopt;
        if (!is_solid_sample(view.field_samples[global_field_index(view, coord)])) return std::nullopt;

        const vec2 center = global_sample_world_position(view, coord);
        const vec2 up     = normalize(center - view.world_center);

        // March outward from the solid sample, then bisect back to a stable surface anchor.
        return march_surface_anchor(
            view,
            coord,
            up,
            std::max(min(view.cell_size) * 0.12f, 0.02f),
            std::max(min(view.cell_size) * 3.0f, 0.35f));
    }
}
