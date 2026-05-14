#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace game::terrain::constants
{
    inline constexpr float undiggable_core_radius_fraction{ 0.3f };
    inline constexpr float hard_rock_depth_threshold{ 1.0f - undiggable_core_radius_fraction };
    inline constexpr float player_spawn_height_offset{ 1.75f };

    inline constexpr float surface_plant_depth_limit{ 0.12f };
    inline constexpr float green_surface_sample_threshold{ 0.25f };

    inline constexpr float cave_smoothing_min_depth{ 0.10f };
    inline constexpr float cave_smoothing_max_depth{ 0.45f };

    inline constexpr float terrain_rock_blend_start_depth{ 0.24f };
    inline constexpr float terrain_rock_blend_end_depth{ 0.88f };

    inline constexpr float shallow_rock_max_depth{ 0.075f };
    inline constexpr float cave_resource_min_depth{ 0.18f };
    inline constexpr float cave_resource_max_depth{ 0.56f };
    inline constexpr float ground_iron_min_depth{ 0.32f };

    inline constexpr int ground_resource_spacing_radius{ 4 };
    inline constexpr int embedded_resource_spacing_radius{ 4 };

    inline constexpr float base_wetness_radius_cells{ 16.0f };
    inline constexpr float pond_wetness_radius_scale{ 5.75f };
    inline constexpr int max_wetness_radius_cells{ 96 };

    inline constexpr float contour_combine_dot_threshold{ 0.9985f };
    inline constexpr float contour_sharp_feature_dot_threshold{ 0.92f };
    inline constexpr float collider_minimum_segment_length_sq{ 1e-4f };

    inline constexpr std::uint8_t dead_bush_resource_family{ 5u };
    inline constexpr std::uint8_t dead_tree_resource_family{ 6u };

    inline constexpr std::array<std::size_t, 5> dry_floor_dead_resource_families{
        0u, 2u, 3u, dead_bush_resource_family, dead_tree_resource_family
    };

    inline constexpr std::array<std::size_t, 5> damp_floor_dead_resource_families{
        1u, 2u, 4u, dead_bush_resource_family, dead_tree_resource_family
    };

    inline constexpr std::array<std::size_t, 5> shelf_dead_resource_families{
        0u, 2u, 3u, dead_bush_resource_family, dead_tree_resource_family
    };
}
