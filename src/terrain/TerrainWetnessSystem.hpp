#pragma once

#include "pch.hpp"


#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainFieldSample.hpp"

namespace game::terrain
{
    struct TerrainSampleBounds final
    {
        ivec2 min{ 0, 0 };
        ivec2 max{ -1, -1 };
    };

    struct TerrainWetnessComponent final
    {
        std::vector<ivec2>  water_cells{};
        TerrainSampleBounds water_bounds{};
        float               max_distance{ 0.0f };
        int                 radius_cells{ 0 };
    };

    namespace terrain_wetness
    {
        inline TerrainSampleBounds clamp_sample_bounds(
            const ivec2 min_coord,
            const ivec2 max_coord,
            const uvec2 field_size)
        {
            return {
                .min = {
                    std::clamp(min_coord.x, 0, static_cast<int>(field_size.x) - 1),
                    std::clamp(min_coord.y, 0, static_cast<int>(field_size.y) - 1)
                },
                .max = {
                    std::clamp(max_coord.x, 0, static_cast<int>(field_size.x) - 1),
                    std::clamp(max_coord.y, 0, static_cast<int>(field_size.y) - 1)
                }
            };
        }

        inline TerrainSampleBounds expand_sample_bounds(
            const TerrainSampleBounds& bounds,
            const int                  radius_cells,
            const uvec2                field_size)
        {
            return clamp_sample_bounds(
                bounds.min - radius_cells,
                bounds.max + radius_cells,
                field_size);
        }

        inline bool sample_bounds_intersect(const TerrainSampleBounds& lhs, const TerrainSampleBounds& rhs)
        {
            return lhs.min.x <= rhs.max.x &&
                    lhs.max.x >= rhs.min.x &&
                    lhs.min.y <= rhs.max.y &&
                    lhs.max.y >= rhs.min.y;
        }

        inline TerrainSampleBounds intersect_sample_bounds(
            const TerrainSampleBounds& lhs,
            const TerrainSampleBounds& rhs)
        {
            return {
                .min = { std::max(lhs.min.x, rhs.min.x), std::max(lhs.min.y, rhs.min.y) },
                .max = { std::min(lhs.max.x, rhs.max.x), std::min(lhs.max.y, rhs.max.y) }
            };
        }

        // find connected water components near the changed area, then give each component an influence radius based on size
        // the recompute pass can then walk outward from those components instead of treating all water as one source
        template <typename CollectWaterComponent>
        std::vector<TerrainWetnessComponent> collect_wetness_components(
            const std::span<const TerrainFieldSample> global_field,
            const uvec2                              global_field_size,
            const TerrainSampleBounds&               discovery_bounds,
            const float                              min_cell_extent,
            const int                                max_wetness_radius_cells,
            CollectWaterComponent&&                  collect_water_component)
        {
            auto field_index = [global_field_size](const ivec2 coord)
            {
                return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(global_field_size.x) +
                        static_cast<std::size_t>(coord.x);
            };

            auto component_wetness_distance = [min_cell_extent, max_wetness_radius_cells
                    ](const std::size_t water_sample_count)
            {
                const float equivalent_radius_cells = std::sqrt(static_cast<float>(water_sample_count) / pi);
                const float radius_cells            = std::clamp(
                    constants::base_wetness_radius_cells +
                        equivalent_radius_cells * constants::pond_wetness_radius_scale,
                    1.0f,
                    static_cast<float>(max_wetness_radius_cells));
                return std::max(radius_cells * min_cell_extent, min_cell_extent);
            };

            std::unordered_set<std::uint64_t>    visited_water;
            std::vector<TerrainWetnessComponent> components;

            for (int y = discovery_bounds.min.y; y <= discovery_bounds.max.y; ++y)
            {
                for (int x = discovery_bounds.min.x; x <= discovery_bounds.max.x; ++x)
                {
                    const ivec2 coord{ x, y };
                    if (!has_water_sample(global_field[field_index(coord)])) continue;

                    const auto key = sample_key(coord);
                    if (!visited_water.insert(key).second) continue;

                    auto water_cells = collect_water_component(coord, false);
                    for (const auto water_coord : water_cells) { visited_water.insert(game::sample_key(water_coord)); }

                    if (water_cells.empty()) continue;

                    TerrainSampleBounds water_bounds{ .min = water_cells.front(), .max = water_cells.front() };

                    for (const auto water_coord : water_cells)
                    {
                        water_bounds.min.x = std::min(water_bounds.min.x, water_coord.x);
                        water_bounds.min.y = std::min(water_bounds.min.y, water_coord.y);
                        water_bounds.max.x = std::max(water_bounds.max.x, water_coord.x);
                        water_bounds.max.y = std::max(water_bounds.max.y, water_coord.y);
                    }

                    const float max_distance = component_wetness_distance(water_cells.size());
                    const int   radius_cells = std::max(
                        1, static_cast<int>(std::ceil(max_distance / std::max(min_cell_extent, 1e-6f))));
                    const auto component_bounds = expand_sample_bounds(water_bounds, radius_cells, global_field_size);
                    if (!sample_bounds_intersect(component_bounds, discovery_bounds)) continue;

                    components.push_back({
                        .water_cells  = std::move(water_cells),
                        .water_bounds = water_bounds,
                        .max_distance = max_distance,
                        .radius_cells = radius_cells
                    });
                }
            }

            return components;
        }

        // local wetness rebuild after terrain or water edits
        // clamp the work to a region around the changed samples, then run outward from nearby water components with a priority queue
        // this is much cheaper than rebuilding the whole planet for a small edit
        template <typename MarkDirty, typename CollectWaterComponent>
        void recompute_around(
            std::span<TerrainFieldSample> global_field,
            const uvec2                   global_field_size,
            const vec2                    terrain_cell_size,
            const std::span<const ivec2>  changed_coords,
            MarkDirty&&                   mark_dirty,
            CollectWaterComponent&&       collect_water_component,
            const bool                    mark_affected_visuals_dirty = false)
        {
            if (changed_coords.empty() || global_field.empty()) return;

            auto field_index = [global_field_size](const ivec2 coord)
            {
                return static_cast<std::size_t>(coord.y) *
                        static_cast<std::size_t>(global_field_size.x) +
                        static_cast<std::size_t>(coord.x);
            };

            auto is_valid_global_sample = [global_field_size](const ivec2 coord)
            {
                return coord.x >= 0 && coord.y >= 0 &&
                        coord.x < static_cast<int>(global_field_size.x) &&
                        coord.y < static_cast<int>(global_field_size.y);
            };

            auto wetness_strength = [](const float distance, const float max_distance)
            {
                if (max_distance <= 1e-6f || distance >= max_distance) return 0.0f;

                const float saturated_band = max_distance * 0.25f;
                if (distance <= saturated_band) return 1.0f;

                const float normalized =
                        std::clamp(1.0f - (distance - saturated_band) / std::max(max_distance - saturated_band, 1e-6f),
                                   0.0f, 1.0f);
                return normalized * normalized * (3.0f - 2.0f * normalized);
            };

            auto bounds_contains = [](const TerrainSampleBounds& bounds, const ivec2 coord)
            {
                return coord.x >= bounds.min.x &&
                        coord.y >= bounds.min.y &&
                        coord.x <= bounds.max.x &&
                        coord.y <= bounds.max.y;
            };

            auto neighbor_distance = [terrain_cell_size](const ivec2 offset)
            {
                return (terrain_cell_size * offset).length();
            };

            static constexpr std::array wetness_neighbors{
                ivec2{ 1, 0 },
                ivec2{ -1, 0 },
                ivec2{ 0, 1 },
                ivec2{ 0, -1 },
                ivec2{ 1, 1 },
                ivec2{ 1, -1 },
                ivec2{ -1, 1 },
                ivec2{ -1, -1 }
            };

            ivec2 changed_min = changed_coords.front();
            ivec2 changed_max = changed_coords.front();
            for (const auto coord : changed_coords)
            {
                changed_min.x = std::min(changed_min.x, coord.x);
                changed_min.y = std::min(changed_min.y, coord.y);
                changed_max.x = std::max(changed_max.x, coord.x);
                changed_max.y = std::max(changed_max.y, coord.y);
            }

            const float          min_cell_extent = min(terrain_cell_size);

            const auto changed_bounds  = clamp_sample_bounds(changed_min, changed_max, global_field_size);

            const auto affected_bounds = expand_sample_bounds(
                changed_bounds,
                constants::max_wetness_radius_cells,
                global_field_size);

            const auto component_discovery_bounds = expand_sample_bounds(
                affected_bounds,
                constants::max_wetness_radius_cells,
                global_field_size);

            auto clear_affected_wetness = [&]
            {
                for (int y = affected_bounds.min.y; y <= affected_bounds.max.y; ++y)
                {
                    for (int x = affected_bounds.min.x; x <= affected_bounds.max.x; ++x)
                    {
                        const ivec2 coord{ x, y };
                        auto&       sample = global_field[field_index(coord)];
                        if (std::abs(sample.wetness) <= 1e-6f) continue;

                        sample.wetness = 0.0f;
                        mark_dirty(coord);
                    }
                }
            };

            auto mark_affected_wetness_visuals_dirty = [&]
            {
                for (int y = affected_bounds.min.y; y <= affected_bounds.max.y; ++y)
                {
                    for (int x = affected_bounds.min.x; x <= affected_bounds.max.x; ++x)
                    {
                        mark_dirty({ x, y });
                    }
                }
            };

            bool has_water_in_discovery   = false;
            bool has_wetness_in_discovery = false;
            for (int y = component_discovery_bounds.min.y;
                 y <= component_discovery_bounds.max.y &&
                 (!has_water_in_discovery || !has_wetness_in_discovery);
                 ++y)
            {
                for (int x = component_discovery_bounds.min.x; x <= component_discovery_bounds.max.x; ++x)
                {
                    const auto& sample       = global_field[field_index({ x, y })];
                    has_water_in_discovery   = has_water_in_discovery || has_water_sample(sample);

                    has_wetness_in_discovery =
                            has_wetness_in_discovery ||
                            (bounds_contains(affected_bounds, { x, y }) &&
                                sample.wetness > 1e-6f);

                    if (has_water_in_discovery && has_wetness_in_discovery) break;
                }
            }

            if (!has_water_in_discovery)
            {
                if (!has_wetness_in_discovery) return;

                clear_affected_wetness();
                if (mark_affected_visuals_dirty) mark_affected_wetness_visuals_dirty();
                return;
            }

            const auto components = collect_wetness_components(
                global_field,
                global_field_size,
                component_discovery_bounds,
                min_cell_extent,
                constants::max_wetness_radius_cells,
                std::forward<CollectWaterComponent>(
                    collect_water_component));

            if (components.empty())
            {
                if (!has_wetness_in_discovery) return;

                clear_affected_wetness();
                if (mark_affected_visuals_dirty) mark_affected_wetness_visuals_dirty();
                return;
            }

            const ivec2 affected_size = affected_bounds.max - affected_bounds.min + 1;
            std::vector best_wetness(
                static_cast<std::size_t>(affected_size.x) * static_cast<std::size_t>(affected_size.y), 0.0f);

            auto affected_index = [affected_bounds, affected_width = affected_size.x](const ivec2 coord)
            {
                const ivec2 local = coord - affected_bounds.min;
                return static_cast<std::size_t>(local.y) *
                        static_cast<std::size_t>(affected_width) +
                        static_cast<std::size_t>(local.x);
            };

            struct WetnessNode final
            {
                ivec2 coord{ 0, 0 };
                float distance{ 0.0f };
            };

            struct WetnessNodeCompare final
            {
                bool operator()(const WetnessNode& lhs, const WetnessNode& rhs) const
                {
                    return lhs.distance > rhs.distance;
                }
            };

            for (const auto& component : components)
            {
                const auto component_influence_bounds = expand_sample_bounds(
                    component.water_bounds,
                    component.radius_cells,
                    global_field_size);

                if (!sample_bounds_intersect(component_influence_bounds, affected_bounds)) continue;

                const auto  propagation_bounds = component_influence_bounds;
                const ivec2 propagation_size   = propagation_bounds.max - propagation_bounds.min + 1;

                std::vector best_distances(
                    static_cast<std::size_t>(propagation_size.x) *
                    static_cast<std::size_t>(propagation_size.y),
                    inf);

                auto propagation_index = [propagation_bounds, propagation_width = propagation_size.x](const ivec2 coord)
                {
                    const ivec2 local = coord - propagation_bounds.min;
                    return static_cast<std::size_t>(local.y) *
                            static_cast<std::size_t>(propagation_width) +
                            static_cast<std::size_t>(local.x);
                };

                std::priority_queue<WetnessNode, std::vector<WetnessNode>, WetnessNodeCompare> frontier;

                auto try_push = [&](const ivec2 coord, const float distance)
                {
                    if (!bounds_contains(propagation_bounds, coord) ||
                        !is_valid_global_sample(coord))
                        return;

                    if (!is_solid_sample(global_field[field_index(coord)])) return;

                    auto& best_distance = best_distances[propagation_index(coord)];
                    if (distance + 1e-5f >= best_distance || distance > component.max_distance) return;

                    best_distance = distance;
                    frontier.push(WetnessNode{ coord, distance });
                };

                for (const auto water_coord : component.water_cells)
                {
                    for (const auto& offset : wetness_neighbors)
                    {
                        try_push(
                            water_coord + offset,
                            neighbor_distance(offset));
                    }
                }

                while (!frontier.empty())
                {
                    const auto [coord, distance] = frontier.top();
                    frontier.pop();

                    if (distance > best_distances[propagation_index(coord)] + 1e-5f) continue;

                    if (bounds_contains(affected_bounds, coord))
                    {
                        best_wetness[affected_index(coord)] =
                                std::max(best_wetness[affected_index(coord)],
                                         wetness_strength(distance, component.max_distance));
                    }

                    for (const auto& offset : wetness_neighbors)
                    {
                        try_push(
                            coord + offset,
                            distance + neighbor_distance(offset));
                    }
                }
            }

            for (int y = affected_bounds.min.y; y <= affected_bounds.max.y; ++y)
            {
                for (int x = affected_bounds.min.x; x <= affected_bounds.max.x; ++x)
                {
                    const ivec2 coord{ x, y };
                    auto&       sample       = global_field[field_index(coord)];
                    const float next_wetness = is_solid_sample(sample) ? best_wetness[affected_index(coord)] : 0.0f;

                    if (std::abs(sample.wetness - next_wetness) <= 1e-6f) continue;

                    sample.wetness = next_wetness;
                    mark_dirty(coord);
                }
            }

            if (mark_affected_visuals_dirty) mark_affected_wetness_visuals_dirty();
        }
    }
}
