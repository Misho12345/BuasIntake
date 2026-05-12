#pragma once

#include "pch.hpp"


#include "tools/TerrainTool.hpp"

namespace game::tools
{
    class TerrainTargetResolver final
    {
    public:
        std::optional<vec2> clamped_tool_world_position(const TerrainToolContext& context) const;
        std::optional<vec2> terrain_tool_hit_world_position(const TerrainToolContext& context) const;
        std::optional<vec2> water_pickup_target_world_position(const TerrainToolContext& context) const;
        std::optional<vec2> water_placement_target_world_position(const TerrainToolContext& context) const;
    };
}
