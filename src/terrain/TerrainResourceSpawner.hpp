#pragma once

#include "pch.hpp"


#include "resources/ResourceNode.hpp"
#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainGenerator.hpp"

namespace game::terrain
{
    class TerrainResourceSpawner final
    {
      public:
        template <typename FieldIndex,
                  typename SolidNeighborCount,
                  typename SampleWorldPosition,
                  typename NormalizedDepth,
                  typename SurfaceAttachmentResolver,
                  typename HasWaterNeighbor,
                  typename IsValidSample,
                  typename AddResourceNode>
        static void generate(const std::span<const TerrainGenerator::FieldSample> global_field,
                             const uvec2 global_field_size,
                             const std::uint32_t seed,
                             FieldIndex&& global_field_index,
                             SolidNeighborCount&& solid_neighbor_count,
                             SampleWorldPosition&& global_sample_world_position,
                             NormalizedDepth&& normalized_depth,
                             SurfaceAttachmentResolver&& exposed_surface_attachment,
                             HasWaterNeighbor&& has_water_neighbor,
                             IsValidSample&& is_valid_global_sample,
                             AddResourceNode&& add_resource_node)
        {
            if (global_field.empty())
                return;

            static constexpr float cave_generation_min_depth{0.14f};
            static constexpr float cave_resource_min_depth{0.18f};
            static constexpr float cave_resource_max_depth{0.56f};
            static constexpr float ground_copper_min_depth{0.24f};
            static constexpr float ground_iron_min_depth{0.32f};
            static constexpr int ground_resource_spacing_radius{4};

            auto is_solid = [](const TerrainGenerator::FieldSample& sample) { return sample.terrain >= 0.0f; };

            auto is_exposed_to_air = [is_solid](const TerrainGenerator::FieldSample& sample, const int solid_neighbors)
            { return is_solid(sample) && solid_neighbors >= 2 && solid_neighbors < 8; };

            auto sample_key = [](const ivec2 coord)
            { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32u) | static_cast<std::uint32_t>(coord.y); };

            auto hash01 = [](const float x, const float y, const std::uint32_t hash_seed)
            {
                const float value = std::sin(x * 12.9898f + y * 78.233f + static_cast<float>(hash_seed) * 0.013f) * 43758.5453f;
                return value - std::floor(value);
            };

            auto fract01 = [](const float value) { return value - std::floor(value); };

            auto terrain_hash = [fract01](vec2 point, const std::uint32_t hash_seed)
            {
                const float seed_offset = static_cast<float>(hash_seed) * 0.0009765625f;
                point = {fract01(point.x * 0.1031f + seed_offset), fract01(point.y * 0.11369f + seed_offset)};
                const float dot_value = point.x * (point.y + 19.19f) + point.y * (point.x + 19.19f);
                point = {point.x + dot_value, point.y + dot_value};
                return fract01((point.x + point.y) * point.x * point.y);
            };

            auto perlin_noise = [terrain_hash](const vec2 point, const std::uint32_t noise_seed)
            {
                const vec2 cell{std::floor(point.x), std::floor(point.y)};
                const vec2 local{point.x - cell.x, point.y - cell.y};

                auto gradient = [&](const vec2 corner)
                {
                    const float angle = terrain_hash(corner, noise_seed) * 2.0f * pi;
                    return vec2{std::cos(angle), std::sin(angle)};
                };

                auto fade = [](const float t) { return t * t * (3.0f - 2.0f * t); };

                const vec2 c00 = cell;
                const vec2 c10{cell.x + 1.0f, cell.y};
                const vec2 c01{cell.x, cell.y + 1.0f};
                const vec2 c11{cell.x + 1.0f, cell.y + 1.0f};

                const float n00 = gradient(c00).dot(local - vec2{0.0f, 0.0f});
                const float n10 = gradient(c10).dot(local - vec2{1.0f, 0.0f});
                const float n01 = gradient(c01).dot(local - vec2{0.0f, 1.0f});
                const float n11 = gradient(c11).dot(local - vec2{1.0f, 1.0f});

                const float u = fade(local.x);
                const float v = fade(local.y);
                const float nx0 = std::lerp(n00, n10, u);
                const float nx1 = std::lerp(n01, n11, u);
                return std::lerp(nx0, nx1, v);
            };

            auto perlin_fbm = [perlin_noise](vec2 point, const std::uint32_t noise_seed)
            {
                float value = 0.0f;
                float amplitude = 0.5f;
                for (int i = 0; i < 5; ++i)
                {
                    value += perlin_noise(point, noise_seed + i * 131u) * amplitude;
                    point = {point.x * 2.04f - 4.8f, point.y * 2.04f + 9.2f};
                    amplitude *= 0.5f;
                }

                return value;
            };

            auto choose_variant_row = [hash01, seed](const ivec2 coord, const std::uint32_t seed_offset)
            {
                return static_cast<std::uint8_t>(std::clamp(
                    static_cast<int>(hash01(static_cast<float>(coord.x), static_cast<float>(coord.y), seed + seed_offset) * 16.0f), 0, 15));
            };

            std::unordered_set<std::uint64_t> occupied_samples;

            auto has_occupied_neighbor = [&](const ivec2 coord, const int radius)
            {
                for (int oy = -radius; oy <= radius; ++oy)
                {
                    for (int ox = -radius; ox <= radius; ++ox)
                    {
                        if (ox == 0 && oy == 0)
                            continue;
                        const ivec2 neighbor{coord.x + ox, coord.y + oy};
                        if (!is_valid_global_sample(neighbor))
                            continue;
                        if (occupied_samples.contains(sample_key(neighbor)))
                            return true;
                    }
                }

                return false;
            };

            auto emit_node = [&](const resources::ResourceNodeKind kind,
                                 const ivec2 coord,
                                 const std::uint8_t variant,
                                 const bool cave_variant,
                                 const bool surface_attached = false,
                                 const vec2 anchor_world = {0.0f, 0.0f},
                                 const vec2 surface_up = {0.0f, 0.0f})
            {
                add_resource_node(kind, coord, variant, cave_variant, surface_attached, anchor_world, surface_up);
                occupied_samples.insert(sample_key(coord));
            };

            for (int y = 1; y < static_cast<int>(global_field_size.y) - 1; ++y)
            {
                for (int x = 1; x < static_cast<int>(global_field_size.x) - 1; ++x)
                {
                    const ivec2 coord{x, y};
                    const auto& sample = global_field[global_field_index(coord)];
                    const int neighbors = solid_neighbor_count(coord);
                    if (!is_exposed_to_air(sample, neighbors))
                        continue;

                    const vec2 world = global_sample_world_position(coord);
                    const float depth = normalized_depth(world);
                    const bool cave = depth > cave_resource_min_depth && depth < cave_resource_max_depth;
                    const auto attachment = exposed_surface_attachment(coord);
                    if (!attachment.has_value())
                        continue;
                    const float alignment = cave ? attachment->floor_alignment : 0.0f;
                    const float roll = hash01(static_cast<float>(x), static_cast<float>(y), seed + 1701u);
                    const float density = !cave ? 0.070f : (alignment > 0.95f ? 0.012f : alignment > 0.86f ? 0.032f : 0.075f);
                    if (roll > density)
                        continue;

                    resources::ResourceNodeKind kind = resources::ResourceNodeKind::Rock;
                    const float ore_roll = hash01(static_cast<float>(x), static_cast<float>(y), seed + 2309u);
                    if (cave && alignment > 0.90f)
                    {
                        kind = resources::ResourceNodeKind::Rock;
                    }
                    else if (cave && depth > 0.44f && ore_roll > 0.84f)
                        kind = resources::ResourceNodeKind::DiamondOre;
                    else if (cave && depth > 0.34f && ore_roll > 0.62f)
                        kind = resources::ResourceNodeKind::GoldOre;
                    else if (cave && depth > 0.22f && ore_roll > 0.44f)
                        kind = resources::ResourceNodeKind::IronOre;
                    else if (cave && ore_roll > 0.20f)
                        kind = resources::ResourceNodeKind::CopperOre;

                    if (!cave && has_occupied_neighbor(coord, ground_resource_spacing_radius))
                        continue;

                    emit_node(kind, coord, choose_variant_row(coord, 19u), cave, true, attachment->anchor_world, attachment->surface_up);
                }
            }

            for (int y = 1; y < static_cast<int>(global_field_size.y) - 1; ++y)
            {
                for (int x = 1; x < static_cast<int>(global_field_size.x) - 1; ++x)
                {
                    const ivec2 coord{x, y};
                    if (occupied_samples.contains(sample_key(coord)) || has_occupied_neighbor(coord, ground_resource_spacing_radius))
                        continue;

                    const auto& sample = global_field[global_field_index(coord)];
                    if (!is_solid(sample))
                        continue;

                    const int neighbors = solid_neighbor_count(coord);
                    if (neighbors < 8)
                        continue;

                    const vec2 world = global_sample_world_position(coord);
                    const float depth = normalized_depth(world);
                    if (depth < cave_generation_min_depth || depth > constants::hard_rock_depth_threshold - 0.03f)
                        continue;

                    const float cluster_noise = perlin_fbm(world * 0.076f + vec2{14.0f, -11.0f}, seed + 3209u);
                    const float seam_noise = std::abs(perlin_noise(world * 0.182f + vec2{-7.0f, 19.0f}, seed + 4513u));
                    const float depth_factor =
                        std::clamp((depth - cave_generation_min_depth) /
                                       std::max(constants::hard_rock_depth_threshold - cave_generation_min_depth - 0.03f, 0.01f),
                                   0.0f,
                                   1.0f);
                    const float upper_stone_factor = 1.0f - std::clamp(depth / ground_copper_min_depth, 0.0f, 1.0f);
                    const float density = 0.0085f + std::max(cluster_noise, 0.0f) * 0.045f + depth_factor * 0.018f +
                                          upper_stone_factor * 0.020f;
                    const float placement_roll = hash01(static_cast<float>(x), static_cast<float>(y), seed + 5003u);
                    if (seam_noise > 0.46f || placement_roll > density)
                        continue;

                    const float ore_roll = hash01(static_cast<float>(x), static_cast<float>(y), seed + 5407u);
                    resources::ResourceNodeKind kind = resources::ResourceNodeKind::Rock;
                    if (depth > 0.50f && ore_roll > 0.86f)
                        kind = resources::ResourceNodeKind::DiamondOre;
                    else if (depth > 0.38f && ore_roll > 0.62f)
                        kind = resources::ResourceNodeKind::GoldOre;
                    else if (depth > ground_iron_min_depth && ore_roll > 0.34f)
                        kind = resources::ResourceNodeKind::IronOre;
                    else if (depth > ground_copper_min_depth && ore_roll > 0.18f)
                        kind = resources::ResourceNodeKind::CopperOre;

                    emit_node(kind, coord, choose_variant_row(coord, 149u), true);
                }
            }

            static constexpr std::array<std::size_t, 5> dry_floor_dead_families{0u, 2u, 3u, 8u, 9u};
            static constexpr std::array<std::size_t, 5> damp_floor_dead_families{1u, 2u, 4u, 8u, 9u};
            static constexpr std::array<std::size_t, 5> shelf_dead_families{0u, 2u, 3u, 5u, 8u};

            for (int y = 1; y < static_cast<int>(global_field_size.y) - 1; ++y)
            {
                for (int x = 1; x < static_cast<int>(global_field_size.x) - 1; ++x)
                {
                    const ivec2 coord{x, y};
                    const auto& sample = global_field[global_field_index(coord)];
                    if (!is_exposed_to_air(sample, solid_neighbor_count(coord)))
                        continue;

                    const vec2 world = global_sample_world_position(coord);
                    const float depth = normalized_depth(world);
                    if (depth < cave_resource_min_depth || depth > cave_resource_max_depth)
                        continue;
                    if (has_water_neighbor(coord))
                        continue;
                    const auto attachment = exposed_surface_attachment(coord);
                    if (!attachment.has_value())
                        continue;
                    const float alignment = attachment->floor_alignment;
                    if (alignment < 0.72f)
                        continue;
                    const float roll = hash01(static_cast<float>(x), static_cast<float>(y), seed + 4073u);
                    const float density = alignment > 0.96f   ? (sample.wetness > 0.18f ? 0.98f : 0.92f)
                                          : alignment > 0.86f ? (sample.wetness > 0.18f ? 0.90f : 0.82f)
                                                              : 0.56f;
                    if (roll > density)
                        continue;

                    if (occupied_samples.contains(sample_key(coord)))
                        continue;

                    const bool damp_family = sample.wetness > 0.18f;
                    const float family_roll = hash01(static_cast<float>(x), static_cast<float>(y), seed + 4483u);
                    const auto& family_pool =
                        alignment > 0.93f ? (damp_family ? damp_floor_dead_families : dry_floor_dead_families) : shelf_dead_families;
                    const auto family_slot = std::min<std::size_t>(
                        static_cast<std::size_t>(family_roll * static_cast<float>(family_pool.size())), family_pool.size() - 1u);
                    std::uint8_t family_index = static_cast<std::uint8_t>(family_pool[family_slot]);

                    const float large_prop_roll = hash01(static_cast<float>(x), static_cast<float>(y), seed + 4937u);
                    if (alignment > 0.988f && large_prop_roll > (damp_family ? 0.86f : 0.68f))
                        family_index = 9u;
                    else if (alignment > 0.94f && large_prop_roll > 0.42f)
                        family_index = 8u;

                    const std::uint8_t variant = static_cast<std::uint8_t>(family_index * 16u + choose_variant_row(coord, 4673u));
                    emit_node(resources::ResourceNodeKind::DeadPlant,
                              coord,
                              variant,
                              true,
                              true,
                              attachment->anchor_world,
                              attachment->surface_up);
                }
            }
        }
    };
}
