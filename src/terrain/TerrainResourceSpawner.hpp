#pragma once

#include "pch.hpp"


#include "resources/ResourceNode.hpp"
#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainFieldSample.hpp"
#include "terrain/TerrainResourceNoise.hpp"
#include "terrain/TerrainResourcePlacement.hpp"

namespace game::terrain
{
    class TerrainResourceSpawner final
    {
    public:
        // turns the generated field into collectable rocks ores and cave decoration
        // it is callback based so it can run over PlanetTerrain's global field without owning the terrain object or resource system
        // chatgpt was used in parts of the tuning and review because this is mostly rule balancing and bug checking

        template <typename FieldIndex,
                  typename SolidNeighborCount,
                  typename SampleWorldPosition,
                  typename NormalizedDepth,
                  typename SurfaceAttachmentResolver,
                  typename HasWaterNeighbor,
                  typename IsValidSample,
                  typename AddResourceNode>
        static void generate(
            const std::span<const TerrainFieldSample> global_field,
            const uvec2                              global_field_size,
            const std::uint32_t                      seed,
            FieldIndex&&                             global_field_index,
            SolidNeighborCount&&                     solid_neighbor_count,
            SampleWorldPosition&&                    global_sample_world_position,
            NormalizedDepth&&                        normalized_depth,
            SurfaceAttachmentResolver&&              exposed_surface_attachment,
            HasWaterNeighbor&&                       has_water_neighbor,
            IsValidSample&&                          is_valid_global_sample,
            AddResourceNode&&                        add_resource_node)
        {
            if (global_field.empty()) return;

            static constexpr float cave_generation_min_depth{ 0.14f };
            static constexpr float cave_resource_min_depth{ 0.18f };
            static constexpr float cave_resource_max_depth{ 0.56f };
            static constexpr float ground_copper_min_depth{ 0.24f };
            static constexpr float ground_iron_min_depth{ 0.32f };
            static constexpr int   ground_resource_spacing_radius{ 4 };

            // depth gates keep cheap ores near the surface and reserve rare ore rolls for deeper stone and caves

            auto is_exposed_to_air = [](const TerrainFieldSample& sample, const int solid_neighbors)
            {
                return is_solid_sample(sample) && solid_neighbors >= 2 && solid_neighbors < 8;
            };

            TerrainResourcePlacement placement;

            auto emit_node = [&](
                const resources::ResourceNodeKind kind,
                const ivec2                       coord,
                const std::uint8_t                variant,
                const bool                        surface_attached = false,
                const vec2                        anchor_world     = { 0.0f, 0.0f },
                const vec2                        surface_up       = { 0.0f, 0.0f })
            {
                add_resource_node(kind, coord, variant, surface_attached, anchor_world, surface_up);
                placement.occupy(coord);
            };

            // exposed rocks and ores on cave and ground surfaces
            for (int y = 1; y < static_cast<int>(global_field_size.y) - 1; ++y)
            {
                for (int x = 1; x < static_cast<int>(global_field_size.x) - 1; ++x)
                {
                    const ivec2 coord{ x, y };
                    const auto& sample    = global_field[global_field_index(coord)];
                    const int   neighbors = solid_neighbor_count(coord);

                    if (!is_exposed_to_air(sample, neighbors)) continue;

                    const vec2  world      = global_sample_world_position(coord);
                    const float depth      = normalized_depth(world);
                    const bool  cave       = depth > cave_resource_min_depth && depth < cave_resource_max_depth;
                    const auto  attachment = exposed_surface_attachment(coord);

                    if (!attachment.has_value()) continue;

                    const float alignment = cave ? attachment->floor_alignment : 0.0f;
                    const float roll      = TerrainResourceNoise::hash01(static_cast<float>(x), static_cast<float>(y), seed + 1701u);
                    const float density   = !cave
                                                ? 0.070f
                                                : (alignment > 0.95f ? 0.012f : alignment > 0.86f ? 0.032f : 0.075f);

                    // cave floors should stay mostly walkable, so flatter alignment gets a lower resource density than walls and rough surfaces
                    if (roll > density) continue;

                    using resources::ResourceNodeKind;

                    auto kind = ResourceNodeKind::Rock;

                    const float ore_roll = TerrainResourceNoise::hash01(
                        static_cast<float>(x),
                        static_cast<float>(y),
                        seed + 2309u);

                    if (cave && alignment > 0.90f) kind = ResourceNodeKind::Rock;
                    else if (cave && depth > 0.44f && ore_roll > 0.84f) kind = ResourceNodeKind::DiamondOre;
                    else if (cave && depth > 0.34f && ore_roll > 0.62f) kind = ResourceNodeKind::GoldOre;
                    else if (cave && depth > 0.22f && ore_roll > 0.44f) kind = ResourceNodeKind::IronOre;
                    else if (cave && ore_roll > 0.20f) kind = ResourceNodeKind::CopperOre;

                    if (!cave && placement.has_occupied_neighbor(coord, ground_resource_spacing_radius, is_valid_global_sample)) continue;

                    emit_node(
                        kind, coord,
                        TerrainResourceNoise::choose_variant_row(coord, seed, 19u),
                        true,
                        attachment->anchor_world,
                        attachment->surface_up);
                }
            }

            // embedded ore in solid stone, so digging has rewards away from cave walls too
            for (int y = 1; y < static_cast<int>(global_field_size.y) - 1; ++y)
            {
                for (int x = 1; x < static_cast<int>(global_field_size.x) - 1; ++x)
                {
                    const ivec2 coord{ x, y };
                    if (placement.is_occupied(coord) ||
                        placement.has_occupied_neighbor(coord, ground_resource_spacing_radius, is_valid_global_sample))
                        continue;

                    const auto& sample = global_field[global_field_index(coord)];
                    if (!is_solid_sample(sample)) continue;

                    const int neighbors = solid_neighbor_count(coord);
                    if (neighbors < 8) continue;

                    const vec2  world = global_sample_world_position(coord);
                    const float depth = normalized_depth(world);
                    if (depth < cave_generation_min_depth ||
                        depth > constants::hard_rock_depth_threshold - 0.03f)
                        continue;

                    const float cluster_noise = TerrainResourceNoise::perlin_fbm(world * 0.076f + vec2{ 14.0f, -11.0f }, seed + 3209u);

                    const float seam_noise =
                            std::abs(TerrainResourceNoise::perlin_noise(world * 0.182f + vec2{ -7.0f, 19.0f }, seed + 4513u));

                    // cluster_noise makes broad ore pockets, seam_noise cuts holes through them so deposits do not become solid carpets

                    const float depth_factor =
                            std::clamp((depth - cave_generation_min_depth) /
                                       std::max(
                                           constants::hard_rock_depth_threshold - cave_generation_min_depth - 0.03f,
                                           0.01f),
                                       0.0f,
                                       1.0f);

                    const float upper_stone_factor = 1.0f - std::clamp(depth / ground_copper_min_depth, 0.0f, 1.0f);

                    const float density = 0.0085f + std::max(cluster_noise, 0.0f) * 0.045f +
                        depth_factor * 0.018f + upper_stone_factor * 0.020f;

                    const float placement_roll = TerrainResourceNoise::hash01(static_cast<float>(x), static_cast<float>(y), seed + 5003u);
                    if (seam_noise > 0.46f || placement_roll > density) continue;


                    const float ore_roll = TerrainResourceNoise::hash01(static_cast<float>(x), static_cast<float>(y), seed + 5407u);

                    using resources::ResourceNodeKind;

                    auto kind = ResourceNodeKind::Rock;
                    if (depth > 0.50f && ore_roll > 0.86f) kind = ResourceNodeKind::DiamondOre;
                    else if (depth > 0.38f && ore_roll > 0.62f) kind = ResourceNodeKind::GoldOre;
                    else if (depth > ground_iron_min_depth && ore_roll > 0.34f) kind = ResourceNodeKind::IronOre;
                    else if (depth > ground_copper_min_depth && ore_roll > 0.18f) kind = ResourceNodeKind::CopperOre;

                    emit_node(kind, coord, TerrainResourceNoise::choose_variant_row(coord, seed, 149u), true);
                }
            }

            static constexpr std::uint8_t dead_bush_family{ 5u };
            static constexpr std::uint8_t dead_tree_family{ 6u };
            // these indexes line up with the dead plant texture layers in VegetationRenderer, split by how damp or flat the cave floor is
            static constexpr std::array<std::size_t, 5> dry_floor_dead_families{ 0u, 2u, 3u, dead_bush_family, dead_tree_family };
            static constexpr std::array<std::size_t, 5> damp_floor_dead_families{ 1u, 2u, 4u, dead_bush_family, dead_tree_family };
            static constexpr std::array<std::size_t, 5> shelf_dead_families{ 0u, 2u, 3u, dead_bush_family, dead_tree_family };

            // cave plants after resource placement has claimed important cells
            for (int y = 1; y < static_cast<int>(global_field_size.y) - 1; ++y)
            {
                for (int x = 1; x < static_cast<int>(global_field_size.x) - 1; ++x)
                {
                    const ivec2 coord{ x, y };
                    const auto& sample = global_field[global_field_index(coord)];
                    if (!is_exposed_to_air(sample, solid_neighbor_count(coord))) continue;

                    const vec2  world = global_sample_world_position(coord);
                    const float depth = normalized_depth(world);

                    if (depth < cave_resource_min_depth || depth > cave_resource_max_depth) continue;
                    if (has_water_neighbor(coord)) continue;

                    const auto attachment = exposed_surface_attachment(coord);

                    if (!attachment.has_value()) continue;

                    const float alignment = attachment->floor_alignment;

                    if (alignment < 0.72f) continue;

                    const float roll = TerrainResourceNoise::hash01(
                        static_cast<float>(x),
                        static_cast<float>(y),
                        seed + 4073u);

                    const float density = alignment > 0.96f
                                              ? (sample.wetness > 0.18f ? 0.36f : 0.30f)
                                              : alignment > 0.86f
                                                    ? (sample.wetness > 0.18f ? 0.28f : 0.22f)
                                                    : 0.14f;
                    if (roll > density) continue;

                    if (placement.is_occupied(coord)) continue;

                    const bool  damp_family = sample.wetness > 0.18f;
                    const float family_roll = TerrainResourceNoise::hash01(
                        static_cast<float>(x),
                        static_cast<float>(y),
                        seed + 4483u);

                    const auto& family_pool =
                            alignment > 0.93f
                                ? (damp_family ? damp_floor_dead_families : dry_floor_dead_families)
                                : shelf_dead_families;

                    // choose a family first, then the low 4 bits pick a sprite row variant inside that family
                    const auto family_slot = std::min(
                        static_cast<std::size_t>(family_roll * static_cast<float>(family_pool.size())),
                        family_pool.size() - 1u);

                    std::uint8_t family_index = static_cast<std::uint8_t>(family_pool[family_slot]);

                    const float large_prop_roll = TerrainResourceNoise::hash01(static_cast<float>(x), static_cast<float>(y), seed + 4937u);

                    if (alignment > 0.988f && large_prop_roll > (damp_family ? 0.96f : 0.92f)) family_index = dead_tree_family;
                    else if (alignment > 0.94f && large_prop_roll > 0.74f) family_index = dead_tree_family;

                    const std::uint8_t variant = static_cast<std::uint8_t>(
                        family_index * 16u + TerrainResourceNoise::choose_variant_row(coord, seed, 4673u));

                    emit_node(resources::ResourceNodeKind::DeadPlant,
                              coord,
                              variant,
                              true,
                              attachment->anchor_world,
                              attachment->surface_up);
                }
            }
        }
    };
}
