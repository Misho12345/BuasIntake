#pragma once

#include "pch.hpp"


#include "terrain/TerrainFieldSample.hpp"
#include "vegetation/Plant.hpp"

namespace game::terrain
{
    namespace terrain_greenness
    {
        template <typename MarkDirty, typename SampleWorldPosition>
        void recompute(
            std::span<TerrainFieldSample>        global_field,
            const uvec2                          global_field_size,
            const vec2                           terrain_cell_size,
            const std::span<const vegetation::Plant> plant_samples,
            const std::span<const std::size_t>   active_plant_indices,
            MarkDirty&&                          mark_dirty,
            SampleWorldPosition&&                global_sample_world_position)
        {
            if (global_field.empty() || plant_samples.empty()) return;

            auto global_field_index = [global_field_size](const ivec2 coord)
            {
                return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(global_field_size.x) +
                        static_cast<std::size_t>(coord.x);
            };

            auto grass_influence_radius_for = [](const vegetation::PlantFamily family)
            {
                switch (family)
                {
                    case vegetation::PlantFamily::Grass: return 1.55f;
                    case vegetation::PlantFamily::Flowers: return 1.35f;
                    case vegetation::PlantFamily::Bush: return 1.50f;
                    case vegetation::PlantFamily::Tree: return 1.70f;
                }

                return 1.0f;
            };

            auto grass_influence_strength_for = [](const vegetation::PlantFamily family)
            {
                switch (family)
                {
                    case vegetation::PlantFamily::Grass: return 1.0f;
                    case vegetation::PlantFamily::Flowers: return 0.92f;
                    case vegetation::PlantFamily::Bush: return 0.78f;
                    case vegetation::PlantFamily::Tree: return 0.66f;
                }

                return 0.7f;
            };

            std::vector next_greenness(global_field.size(), 0.0f);
            for (const auto index : active_plant_indices)
            {
                if (index >= plant_samples.size()) continue;
                const auto& plant = plant_samples[index];
                if (plant.stage == vegetation::PlantStage::Empty ||
                    plant.stage == vegetation::PlantStage::Seeded)
                    continue;

                const ivec2 coord{
                    static_cast<int>(index % global_field_size.x),
                    static_cast<int>(index / global_field_size.x)
                };

                const auto& sample = global_field[index];
                if (!is_solid_sample(sample)) continue;

                const float wetness_factor = std::clamp((sample.wetness - 0.10f) / 0.30f, 0.0f, 1.0f);
                if (wetness_factor <= 1e-4f) continue;

                const float stage_factor    = plant.stage == vegetation::PlantStage::Mature ? 1.0f : 0.80f;
                const float family_strength = grass_influence_strength_for(plant.family);
                const float radius_world    = grass_influence_radius_for(plant.family);

                const int   radius_x        = std::max(
                    1, static_cast<int>(std::ceil(radius_world / std::max(terrain_cell_size.x, 1e-4f))));

                const int radius_y = std::max(
                    1, static_cast<int>(std::ceil(radius_world / std::max(terrain_cell_size.y, 1e-4f))));

                const vec2 center = plant.anchor_world.lengthSquared() > 1e-6f
                                        ? plant.anchor_world
                                        : global_sample_world_position(coord);

                for (int y = std::max(0, coord.y - radius_y); y <= std::min(
                         static_cast<int>(global_field_size.y) - 1,
                         coord.y + radius_y);
                     ++y)
                {
                    for (int x = std::max(0, coord.x - radius_x);
                         x <= std::min(static_cast<int>(global_field_size.x) - 1,
                                       coord.x + radius_x);
                         ++x)
                    {
                        const ivec2 target_coord{ x, y };
                        const auto  target_index = global_field_index(target_coord);
                        if (!is_solid_sample(global_field[target_index])) continue;

                        const vec2  world    = global_sample_world_position(target_coord);
                        const vec2  delta    = world - center;
                        const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                        if (distance >= radius_world) continue;

                        float falloff = 0.0f;
                        if (distance <= radius_world * 0.72f) falloff = 1.0f;
                        else
                        {
                            const float edge_t = 1.0f -
                                    (distance - radius_world * 0.72f) /
                                    std::max(radius_world * 0.28f, 1e-4f);

                            const float clamped_t = std::clamp(edge_t, 0.0f, 1.0f);
                            falloff               = clamped_t * clamped_t * (3.0f - 2.0f * clamped_t);
                        }

                        const float influence = std::clamp(
                            falloff * wetness_factor * stage_factor * family_strength,
                            0.0f, 1.0f);

                        next_greenness[target_index] = std::max(next_greenness[target_index], influence);
                    }
                }
            }

            for (int y = 0; y < static_cast<int>(global_field_size.y); ++y)
            {
                for (int x = 0; x < static_cast<int>(global_field_size.x); ++x)
                {
                    const ivec2 coord{ x, y };
                    auto&       sample    = global_field[global_field_index(coord)];
                    const float greenness = is_solid_sample(sample) ? next_greenness[global_field_index(coord)] : 0.0f;
                    if (std::abs(sample.greenness - greenness) <= 1e-6f) continue;

                    sample.greenness = greenness;
                    mark_dirty(coord);
                }
            }
        }
    }
}
