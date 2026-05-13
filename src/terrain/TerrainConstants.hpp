#pragma once

namespace game::terrain::constants
{
    inline constexpr float undiggable_core_radius_fraction{ 0.3f };
    inline constexpr float hard_rock_depth_threshold{ 1.0f - undiggable_core_radius_fraction };
    inline constexpr float player_spawn_height_offset{ 1.75f };
}
