#include "pch.hpp"

#include "terrain/TerrainGenerationFinalizer.hpp"

#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainResourceSpawner.hpp"
#include "water/WaterInteraction.hpp"

namespace game::terrain
{
    namespace
    {
        constexpr float cave_generation_min_depth{0.14f};
        constexpr float cave_generation_max_depth{0.54f};

        bool has_water(const TerrainGenerator::FieldSample& sample)
        {
            return water::has_water(sample);
        }
    }

    std::vector<ivec2> terrain_generation_finalizer::initialize_visual_channels_and_smooth_caves(
        TerrainGenerationFieldView view, const TerrainGenerationCallbacks& callbacks)
    {
        std::vector<ivec2> changed_coords;
        if (view.global_field.empty())
            return changed_coords;

        changed_coords.reserve(view.global_field.size() / 12u);
        // Reset visual channels here so the generated field starts from a clean gameplay-ready state.
        for (int y = 0; y < static_cast<int>(view.global_field_size.y); ++y)
        {
            for (int x = 0; x < static_cast<int>(view.global_field_size.x); ++x)
            {
                const ivec2 coord{x, y};
                auto& sample = view.global_field[callbacks.global_field_index(coord)];
                sample.wetness = 0.0f;
                sample.greenness = 0.0f;
                if (has_water(sample))
                    changed_coords.push_back(coord);
            }
        }

        for (int smooth_pass = 0; smooth_pass < 2; ++smooth_pass)
        {
            std::vector<float> smoothed_terrain(view.global_field.size(), 0.0f);
            std::vector<float> smoothed_water(view.global_field.size(), 0.0f);
            for (std::size_t i = 0; i < view.global_field.size(); ++i)
            {
                smoothed_terrain[i] = view.global_field[i].terrain;
                smoothed_water[i] = view.global_field[i].water;
            }

            for (int y = 1; y < static_cast<int>(view.global_field_size.y) - 1; ++y)
            {
                for (int x = 1; x < static_cast<int>(view.global_field_size.x) - 1; ++x)
                {
                    const ivec2 coord{x, y};
                    const vec2 world = callbacks.global_sample_world_position(coord);
                    const float depth = callbacks.normalized_depth(world);
                    if (depth < cave_generation_min_depth || depth > cave_generation_max_depth)
                        continue;

                    float terrain_total = 0.0f;
                    float terrain_weight = 0.0f;
                    float water_total = 0.0f;
                    float water_weight = 0.0f;
                    bool near_boundary = false;

                    for (int oy = -1; oy <= 1; ++oy)
                    {
                        for (int ox = -1; ox <= 1; ++ox)
                        {
                            const ivec2 neighbor{x + ox, y + oy};
                            const auto& neighbor_sample = view.global_field[callbacks.global_field_index(neighbor)];
                            const float weight = (ox == 0 && oy == 0) ? 2.0f : 1.0f;
                            terrain_total += neighbor_sample.terrain * weight;
                            terrain_weight += weight;

                            if (neighbor_sample.water > 0.0f)
                            {
                                water_total += neighbor_sample.water * weight;
                                water_weight += weight;
                            }

                            if ((neighbor_sample.terrain >= 0.0f) !=
                                (view.global_field[callbacks.global_field_index(coord)].terrain >= 0.0f))
                                near_boundary = true;
                        }
                    }

                    // Only smooth cave boundaries so the interior keeps its shape and chunk edges stay stable.
                    if (!near_boundary)
                        continue;

                    const auto index = callbacks.global_field_index(coord);
                    smoothed_terrain[index] =
                        std::lerp(view.global_field[index].terrain, terrain_total / std::max(terrain_weight, 1e-4f), 0.42f);
                    if (water_weight > 0.0f)
                    {
                        smoothed_water[index] = std::lerp(view.global_field[index].water, water_total / water_weight, 0.55f);
                    }
                }
            }

            for (int y = 1; y < static_cast<int>(view.global_field_size.y) - 1; ++y)
            {
                for (int x = 1; x < static_cast<int>(view.global_field_size.x) - 1; ++x)
                {
                    const ivec2 coord{x, y};
                    const auto index = callbacks.global_field_index(coord);
                    const float next_terrain = smoothed_terrain[index];
                    const float next_water = smoothed_water[index];
                    if (std::abs(view.global_field[index].terrain - next_terrain) <= 1e-5f &&
                        std::abs(view.global_field[index].water - next_water) <= 1e-5f)
                        continue;

                    view.global_field[index].terrain = next_terrain;
                    view.global_field[index].water = next_terrain < 0.0f && next_water > 0.0f
                                                       ? next_water
                                                       : callbacks.dry_water_density(view.global_field[index]);
                    changed_coords.push_back(coord);
                }
            }
        }

        return changed_coords;
    }

    void terrain_generation_finalizer::generate_resource_nodes(const TerrainGenerationFieldView& view,
                                                               const TerrainGenerationCallbacks& callbacks)
    {
        TerrainResourceSpawner::generate(view.global_field,
                                         view.global_field_size,
                                         view.seed,
                                         callbacks.global_field_index,
                                         callbacks.solid_neighbor_count,
                                         callbacks.global_sample_world_position,
                                         callbacks.normalized_depth,
                                         callbacks.exposed_surface_attachment,
                                         callbacks.has_water_neighbor,
                                         callbacks.is_valid_global_sample,
                                         [&callbacks](const resources::ResourceNodeKind kind,
                                                      const ivec2 coord,
                                                      const std::uint8_t variant,
                                                      const bool cave_variant,
                                                      const bool surface_attached,
                                                      const vec2 anchor_world,
                                                      const vec2 surface_up)
                                         {
                                             callbacks.add_resource_node({
                                                 .kind = kind,
                                                 .coord = coord,
                                                 .variant = variant,
                                                 .cave_variant = cave_variant,
                                                 .surface_attached = surface_attached,
                                                 .anchor_world = anchor_world,
                                                 .surface_up = surface_up
                                             });
                                         });
    }
}
