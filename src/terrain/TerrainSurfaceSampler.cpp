#include "pch.hpp"

#include "terrain/TerrainSurfaceSampler.hpp"

namespace game::terrain
{
    namespace
    {
        bool is_solid(const TerrainGenerator::FieldSample& sample)
        {
            return sample.terrain >= 0.0f;
        }

        bool field_ready(const TerrainSurfaceFieldView& view)
        {
            return !view.field_samples.empty() && view.field_size.x > 0u && view.field_size.y > 0u;
        }

        bool is_valid_global_sample(const TerrainSurfaceFieldView& view, const ivec2 coord)
        {
            return coord.x >= 0 && coord.y >= 0 && coord.x < static_cast<int>(view.field_size.x) &&
                   coord.y < static_cast<int>(view.field_size.y);
        }

        std::size_t global_field_index(const TerrainSurfaceFieldView& view, const ivec2 coord)
        {
            return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(view.field_size.x) + static_cast<std::size_t>(coord.x);
        }

        vec2 global_sample_world_position(const TerrainSurfaceFieldView& view, const ivec2 coord)
        {
            return {
                view.field_origin.x + static_cast<float>(coord.x) * view.cell_size.x,
                view.field_origin.y + static_cast<float>(coord.y) * view.cell_size.y
            };
        }

        float normalized_depth(const TerrainSurfaceFieldView& view, const vec2 world_position)
        {
            const float surface_radius = std::max(view.planet_radius, 1e-4f);
            const vec2 offset = world_position - view.world_center;
            const float radius = std::sqrt(offset.x * offset.x + offset.y * offset.y);
            return std::clamp(1.0f - radius / surface_radius, 0.0f, 1.0f);
        }

        ivec2 world_to_global_sample(const TerrainSurfaceFieldView& view, const vec2 world_position)
        {
            const float gx = (world_position.x - view.field_origin.x) / view.cell_size.x;
            const float gy = (world_position.y - view.field_origin.y) / view.cell_size.y;

            return {
                std::clamp(static_cast<int>(std::lround(gx)), 0, static_cast<int>(view.field_size.x) - 1),
                std::clamp(static_cast<int>(std::lround(gy)), 0, static_cast<int>(view.field_size.y) - 1)
            };
        }

        std::optional<vec2> march_surface_anchor(
            const TerrainSurfaceFieldView& view, const ivec2 coord, const vec2 direction, const float step_size, const float max_distance)
        {
            const vec2 center = global_sample_world_position(view, coord);
            vec2 last_solid = center;

            for (float distance = step_size; distance <= max_distance; distance += step_size)
            {
                const vec2 probe = center + direction * distance;
                const auto probe_coord = world_to_global_sample(view, probe);
                if (!is_valid_global_sample(view, probe_coord) || !is_solid(view.field_samples[global_field_index(view, probe_coord)]))
                {
                    vec2 low = last_solid;
                    vec2 high = probe;
                    for (int iteration = 0; iteration < 8; ++iteration)
                    {
                        const vec2 mid = lerp(low, high, 0.5f);
                        const auto mid_coord = world_to_global_sample(view, mid);
                        if (is_valid_global_sample(view, mid_coord) && is_solid(view.field_samples[global_field_index(view, mid_coord)]))
                            low = mid;
                        else
                            high = mid;
                    }

                    return lerp(low, high, 0.5f);
                }

                last_solid = probe;
            }

            return std::nullopt;
        }
    }

    bool terrain_surface_sampler::is_surface_exposed_world(const TerrainSurfaceFieldView& view,
                                                           const vec2 world_position,
                                                           const float clearance_distance)
    {
        if (!field_ready(view))
            return false;

        // This cutoff keeps planting and attachment queries near the playable shell instead of deep caves.
        if (normalized_depth(view, world_position) > 0.12f)
            return false;

        const vec2 up = normalize(world_position - view.world_center);
        const float step_size = std::max(std::min(view.cell_size.x, view.cell_size.y) * 0.35f, 0.05f);
        const vec2 start = world_position + up * step_size;

        for (float distance = 0.0f; distance <= clearance_distance; distance += step_size)
        {
            const vec2 probe_world = start + up * distance;
            const auto probe_coord = world_to_global_sample(view, probe_world);
            if (!is_valid_global_sample(view, probe_coord))
                return true;
            if (is_solid(view.field_samples[global_field_index(view, probe_coord)]))
                return false;
        }

        return true;
    }

    std::optional<TerrainSurfaceAttachment> terrain_surface_sampler::exposed_surface_attachment(const TerrainSurfaceFieldView& view,
                                                                                                const ivec2 coord)
    {
        if (!field_ready(view))
            return std::nullopt;
        if (!is_valid_global_sample(view, coord))
            return std::nullopt;
        if (!is_solid(view.field_samples[global_field_index(view, coord)]))
            return std::nullopt;

        const vec2 center = global_sample_world_position(view, coord);
        const vec2 radial_up = normalize(center - view.world_center);
        vec2 surface_up{0.0f, 0.0f};
        int open_neighbors = 0;

        for (int oy = -1; oy <= 1; ++oy)
        {
            for (int ox = -1; ox <= 1; ++ox)
            {
                if (ox == 0 && oy == 0)
                    continue;

                const ivec2 neighbor{coord.x + ox, coord.y + oy};
                if (!is_valid_global_sample(view, neighbor))
                    continue;
                if (is_solid(view.field_samples[global_field_index(view, neighbor)]))
                    continue;

                surface_up += normalize(global_sample_world_position(view, neighbor) - center, {0.0f, 0.0f});
                ++open_neighbors;
            }
        }

        if (open_neighbors == 0)
            return std::nullopt;

        // Use the surrounding open cells to estimate the local outward direction, then march to the actual surface.
        surface_up = normalize(surface_up, radial_up);
        const auto anchor = march_surface_anchor(view,
                                                 coord,
                                                 surface_up,
                                                 std::max(std::min(view.cell_size.x, view.cell_size.y) * 0.10f, 0.02f),
                                                 std::max(std::min(view.cell_size.x, view.cell_size.y) * 3.4f, 0.45f));
        if (!anchor.has_value())
            return std::nullopt;

        return TerrainSurfaceAttachment{
            .anchor_world = *anchor,
            .surface_up = surface_up,
            .floor_alignment = std::clamp(surface_up.dot(radial_up), -1.0f, 1.0f)
        };
    }

    std::optional<vec2> terrain_surface_sampler::surface_anchor_world(const TerrainSurfaceFieldView& view, const ivec2 coord)
    {
        if (!field_ready(view))
            return std::nullopt;
        if (!is_valid_global_sample(view, coord))
            return std::nullopt;
        if (!is_solid(view.field_samples[global_field_index(view, coord)]))
            return std::nullopt;

        const vec2 center = global_sample_world_position(view, coord);
        const vec2 up = normalize(center - view.world_center);
        // March outward from the solid sample, then bisect back to a stable surface anchor.
        return march_surface_anchor(view,
                                    coord,
                                    up,
                                    std::max(std::min(view.cell_size.x, view.cell_size.y) * 0.12f, 0.02f),
                                    std::max(std::min(view.cell_size.x, view.cell_size.y) * 3.0f, 0.35f));
    }
}
