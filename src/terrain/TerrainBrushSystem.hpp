#pragma once

#include "pch.hpp"


#include "terrain/ChunkSettings.hpp"
#include "terrain/TerrainFieldSample.hpp"
#include "terrain/TerrainGenerator.hpp"
#include "water/WaterInteraction.hpp"

namespace game::terrain
{
    class TerrainBrushSystem final
    {
    public:
        struct Result final
        {
            bool          changed{ false };
            std::uint32_t units{ 0u };
            bool          water_changed{ false };
            bool          requires_wetness_rebuild{ false };
        };

        template <typename IsDigProtected,
                  typename HasWaterNeighbor,
                  typename ClampTerrainDensity,
                  typename DryWaterDensity,
                  typename SampleWorldPosition,
                  typename MarkDirtyChunk,
                  typename ClearSolidSample>
        static Result apply_edit(
            std::span<TerrainFieldSample>            global_field,
            const uvec2                              global_field_size,
            const vec2                               global_field_origin,
            const vec2                               terrain_cell_size,
            const vec2                               grid_min,
            const vec2                               grid_max,
            const ChunkSettings&                     settings,
            const TerrainGenerator::TerrainEdit&     edit,
            std::vector<bool>&                       dirty_chunks,
            std::vector<ivec2>&                      changed_coords,
            const std::uint32_t                      unit_budget,
            const std::optional<GroundBrushBlocker>& blocker,
            IsDigProtected&&                         is_dig_protected,
            HasWaterNeighbor&&                       has_water_neighbor,
            ClampTerrainDensity&&                    clamp_terrain_density,
            DryWaterDensity&&                        dry_water_density,
            SampleWorldPosition&&                    sample_world_position,
            MarkDirtyChunk&&                         mark_dirty_chunk,
            ClearSolidSample&&                       clear_solid_sample)
        {
            const float radius = std::max(edit.position_radius_strength.z, 0.0f);
            if (radius <= 0.0f || unit_budget == 0u) return {};

            const vec2 edit_center{ edit.position_radius_strength.x, edit.position_radius_strength.y };
            if (!circle_overlaps_rect(edit_center, radius, grid_min, grid_max)) return {};

            const float signed_strength  = edit.position_radius_strength.w;
            const float falloff_exponent = std::max(edit.falloff_exponent, 0.001f);
            const bool  digging          = signed_strength < 0.0f;

            auto global_field_index = [global_field_size](const ivec2 coord)
            {
                return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(global_field_size.x) +
                        static_cast<std::size_t>(coord.x);
            };

            struct Candidate final
            {
                ivec2 coord{ 0, 0 };
                float falloff{ 0.0f };
                float distance_to_center{ 0.0f };
                bool  affects_water{ false };
            };

            Result                 result{};
            std::vector<Candidate> candidates;
            collect_candidates<Candidate>(
                global_field,
                global_field_size,
                global_field_origin,
                terrain_cell_size,
                settings,
                edit_center,
                radius,
                signed_strength,
                falloff_exponent,
                digging,
                blocker,
                global_field_index,
                std::forward<IsDigProtected>(is_dig_protected),
                std::forward<HasWaterNeighbor>(has_water_neighbor),
                std::forward<ClampTerrainDensity>(clamp_terrain_density),
                std::forward<DryWaterDensity>(dry_water_density),
                std::forward<SampleWorldPosition>(sample_world_position),
                candidates,
                result.requires_wetness_rebuild);

            if (candidates.empty()) return result;

            std::ranges::sort(
                candidates,
                [](const Candidate& lhs, const Candidate& rhs)
                {
                    if (std::abs(lhs.falloff - rhs.falloff) > 1e-6f) return lhs.falloff > rhs.falloff;
                    return lhs.distance_to_center < rhs.distance_to_center;
                });

            apply_candidates<Candidate>(
                global_field,
                settings,
                candidates,
                signed_strength,
                unit_budget,
                global_field_index,
                std::forward<ClampTerrainDensity>(clamp_terrain_density),
                std::forward<DryWaterDensity>(dry_water_density),
                std::forward<SampleWorldPosition>(sample_world_position),
                std::forward<MarkDirtyChunk>(mark_dirty_chunk),
                std::forward<ClearSolidSample>(clear_solid_sample),
                dirty_chunks,
                changed_coords,
                result);

            return result;
        }

    private:
        static bool circle_overlaps_rect(const vec2 center, const float radius, const vec2 rect_min,
                                         const vec2 rect_max)
        {
            const float closest_x = std::clamp(center.x, rect_min.x, rect_max.x);
            const float closest_y = std::clamp(center.y, rect_min.y, rect_max.y);
            const float dx        = center.x - closest_x;
            const float dy        = center.y - closest_y;
            return dx * dx + dy * dy <= radius * radius;
        }

        static bool point_inside_brush_blocker(const vec2 point, const GroundBrushBlocker& blocker, const vec2 padding)
        {
            if (blocker.radius > 0.0f)
            {
                const vec2  delta         = point - blocker.center;
                const float padded_radius = blocker.radius + std::max(padding.x, padding.y);
                return delta.lengthSquared() <= padded_radius * padded_radius;
            }

            const vec2  delta   = point - blocker.center;
            const float local_x = delta.dot(blocker.right);
            const float local_y = delta.dot(blocker.up);
            return std::abs(local_x) <= blocker.half_extents.x + padding.x && std::abs(local_y) <= blocker.half_extents.
                    y + padding.y;
        }

        template <typename Candidate,
                  typename FieldIndex,
                  typename IsDigProtected,
                  typename HasWaterNeighbor,
                  typename ClampTerrainDensity,
                  typename DryWaterDensity,
                  typename SampleWorldPosition>
        static void collect_candidates(const std::span<TerrainFieldSample>      global_field,
                                       const uvec2                              global_field_size,
                                       const vec2                               global_field_origin,
                                       const vec2                               terrain_cell_size,
                                       const ChunkSettings&                     settings,
                                       const vec2                               edit_center,
                                       const float                              radius,
                                       const float                              signed_strength,
                                       const float                              falloff_exponent,
                                       const bool                               digging,
                                       const std::optional<GroundBrushBlocker>& blocker,
                                       FieldIndex&&                             global_field_index,
                                       IsDigProtected&&                         is_dig_protected,
                                       HasWaterNeighbor&&                       has_water_neighbor,
                                       ClampTerrainDensity&&                    clamp_terrain_density,
                                       DryWaterDensity&&                        dry_water_density,
                                       SampleWorldPosition&&                    sample_world_position,
                                       std::vector<Candidate>&                  candidates,
                                       bool&                                    requires_wetness_rebuild)
        {
            const auto min_x = static_cast<int>(std::floor(
                (edit_center.x - radius - global_field_origin.x) / terrain_cell_size.x));
            const auto min_y = static_cast<int>(std::floor(
                (edit_center.y - radius - global_field_origin.y) / terrain_cell_size.y));
            const auto max_x = static_cast<int>(std::ceil(
                (edit_center.x + radius - global_field_origin.x) / terrain_cell_size.x));
            const auto max_y = static_cast<int>(std::ceil(
                (edit_center.y + radius - global_field_origin.y) / terrain_cell_size.y));

            const int clamped_min_x = std::clamp(min_x, 0, static_cast<int>(global_field_size.x) - 1);
            const int clamped_min_y = std::clamp(min_y, 0, static_cast<int>(global_field_size.y) - 1);
            const int clamped_max_x = std::clamp(max_x, 0, static_cast<int>(global_field_size.x) - 1);
            const int clamped_max_y = std::clamp(max_y, 0, static_cast<int>(global_field_size.y) - 1);

            candidates.reserve(
                static_cast<std::size_t>((clamped_max_x - clamped_min_x + 1) * (clamped_max_y - clamped_min_y + 1)));

            for (int y = clamped_min_y; y <= clamped_max_y; ++y)
            {
                for (int x = clamped_min_x; x <= clamped_max_x; ++x)
                {
                    const ivec2 coord{ x, y };
                    const vec2  world              = sample_world_position(coord);
                    const vec2  delta              = world - edit_center;
                    const float distance_to_center = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                    if (distance_to_center >= radius) continue;
                    if (blocker.has_value() &&
                        point_inside_brush_blocker(world, *blocker, {
                                                       terrain_cell_size.x * 0.35f, terrain_cell_size.y * 0.35f
                                                   })) { continue; }

                    if (digging && is_dig_protected(coord)) continue;

                    const float normalized = 1.0f - distance_to_center / radius;
                    const float falloff    = std::pow(normalized, falloff_exponent);
                    if (falloff <= 1e-6f) continue;

                    const auto& sample             = global_field[global_field_index(coord)];
                    const bool  had_water          = has_water_sample(sample);
                    const bool  had_wetness        = sample.wetness > 1e-4f;
                    const bool  had_water_adjacent = has_water_neighbor(coord);
                    const float next_terrain       = clamp_terrain_density(sample.terrain + signed_strength * falloff,
                                                                           world,
                                                                           settings);
                    float next_water = sample.water;
                    if (next_terrain >= 0.0f || !had_water)
                    {
                        next_water = dry_water_density(TerrainFieldSample{
                            .terrain   = next_terrain, .water = sample.water, .wetness = sample.wetness,
                            .greenness = sample.greenness
                        });
                    }

                    const bool local_changed =
                            std::abs(next_terrain - sample.terrain) > 1e-6f || std::abs(next_water - sample.water) >
                            1e-6f;
                    if (!local_changed) continue;

                    candidates.push_back({
                        .coord              = coord,
                        .falloff            = falloff,
                        .distance_to_center = distance_to_center,
                        .affects_water      = had_water || had_water_adjacent
                    });

                    if (had_water || had_wetness || had_water_adjacent) requires_wetness_rebuild = true;
                }
            }
        }

        template <typename Candidate,
                  typename FieldIndex,
                  typename ClampTerrainDensity,
                  typename DryWaterDensity,
                  typename SampleWorldPosition,
                  typename MarkDirtyChunk,
                  typename ClearSolidSample>
        static void apply_candidates(std::span<TerrainFieldSample>        global_field,
                                     const ChunkSettings&                 settings,
                                     const std::vector<Candidate>&        candidates,
                                     const float                          signed_strength,
                                     const std::uint32_t                  unit_budget,
                                     FieldIndex&&                         global_field_index,
                                     ClampTerrainDensity&&                clamp_terrain_density,
                                     DryWaterDensity&&                    dry_water_density,
                                     SampleWorldPosition&&                sample_world_position,
                                     MarkDirtyChunk&&                     mark_dirty_chunk,
                                     ClearSolidSample&&                   clear_solid_sample,
                                     std::vector<bool>&                   dirty_chunks,
                                     std::vector<ivec2>&                  changed_coords,
                                     Result&                              result)
        {
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                if (result.units >= unit_budget) break;

                const auto  coord        = candidates[i].coord;
                const vec2  world        = sample_world_position(coord);
                const auto  sample_index = global_field_index(coord);
                auto&       sample       = global_field[sample_index];
                const bool  had_water    = has_water_sample(sample);
                const bool  was_solid    = is_solid_sample(sample);
                const float next_terrain = clamp_terrain_density(sample.terrain + signed_strength * candidates[i].
                                                                  falloff,
                                                                  world,
                                                                  settings);
                bool local_changed = std::abs(next_terrain - sample.terrain) > 1e-6f;
                sample.terrain     = next_terrain;

                if (sample.terrain >= 0.0f || !had_water)
                {
                    const float next_water    = dry_water_density(sample);
                    const bool  water_changed = std::abs(next_water - sample.water) > 1e-6f;
                    local_changed             = water_changed || local_changed;
                    sample.water              = next_water;
                    result.water_changed      = result.water_changed || (water_changed && candidates[i].affects_water);
                }

                if (!local_changed) continue;
                const bool is_solid_now = is_solid_sample(sample);

                result.water_changed = result.water_changed || candidates[i].affects_water;

                result.changed = true;
                if ((signed_strength < 0.0f && was_solid && !is_solid_now) || (signed_strength > 0.0f && !was_solid &&
                    is_solid_now)) { ++result.units; }

                changed_coords.push_back(coord);
                mark_dirty_chunk(coord, dirty_chunks);

                if (was_solid && !is_solid_now)
                {
                    clear_solid_sample(coord, sample_index);
                }
            }
        }
    };
}
