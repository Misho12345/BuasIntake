#pragma once

#include "pch.hpp"

namespace game::tools
{
    inline constexpr std::size_t max_tool_material_index = 2u;
    inline constexpr std::size_t max_tool_level_index    = 2u;
    inline constexpr std::size_t tool_levels_per_material = 3u;

    constexpr bool at_max_tool_upgrade(const std::size_t material, const std::size_t level)
    {
        return material >= max_tool_material_index &&
                level >= max_tool_level_index;
    }

    constexpr std::pair<std::size_t, std::size_t> next_tool_material_level(
        std::size_t material,
        std::size_t level)
    {
        if (at_max_tool_upgrade(material, level)) return { max_tool_material_index, max_tool_level_index };

        ++level;
        if (level >= tool_levels_per_material)
        {
            level    = 0u;
            material = std::min(material + 1u, max_tool_material_index);
        }

        return { material, level };
    }

    constexpr std::size_t flat_tool_tier_index(const std::size_t material, const std::size_t level)
    {
        return std::min(material, max_tool_material_index) * tool_levels_per_material +
               std::min(level, max_tool_level_index);
    }
}
