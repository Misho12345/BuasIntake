#pragma once

#include "pch.hpp"

#include "resources/Inventory.hpp"
#include "tools/TerrainSculptTool.hpp"
#include "tools/WaterTool.hpp"
#include "ui/UpgradeMenu.hpp"

namespace game::tools::upgrade_model
{
    inline std::pair<std::size_t, std::size_t> next_material_level(
        std::size_t       material,
        std::size_t       level,
        const std::size_t max_material)
    {
        ++level;
        if (level >= 3u)
        {
            level    = 0u;
            material = std::min(material + 1u, max_material);
        }

        return { material, level };
    }

    inline resources::ResourceInventory digging_upgrade_cost(const TerrainSculptTool& tool)
    {
        resources::ResourceInventory cost{};
        auto [next_material, next_level] = next_material_level(tool.material_index(), tool.level_index(), 2u);

        if (next_material == 0u) cost.copper_bars = 2u + static_cast<std::uint32_t>(next_level);
        else if (next_material == 1u)
        {
            cost.copper_bars = 3u + static_cast<std::uint32_t>(next_level);
            cost.iron_bars   = 2u + static_cast<std::uint32_t>(next_level);
        }
        else
        {
            cost.iron_bars = 4u + static_cast<std::uint32_t>(next_level);
            cost.gold_bars = 2u + static_cast<std::uint32_t>(next_level);
            cost.diamonds  = next_level < 2u ? 1u : 2u;
        }

        return cost;
    }

    inline resources::ResourceInventory bucket_upgrade_cost(const WaterTool& tool)
    {
        resources::ResourceInventory cost{};
        auto [next_material, next_level] = next_material_level(tool.material_index(), tool.level_index(), 2u);

        if (next_material == 0u) { cost.rocks = 4u + static_cast<std::uint32_t>(next_level) * 2u; }
        else if (next_material == 1u)
        {
            cost.rocks       = 4u + static_cast<std::uint32_t>(next_level) * 2u;
            cost.copper_bars = 2u + static_cast<std::uint32_t>(next_level);
        }
        else
        {
            cost.rocks       = 6u + static_cast<std::uint32_t>(next_level) * 2u;
            cost.copper_bars = 2u + static_cast<std::uint32_t>(next_level);
            cost.iron_bars   = 2u + static_cast<std::uint32_t>(next_level);
        }

        return cost;
    }

    inline std::string format_cost(const resources::ResourceInventory& cost)
    {
        std::vector<std::string> parts;
        if (cost.rocks != 0u) parts.push_back(std::format("{} rock", cost.rocks));
        if (cost.copper_bars != 0u) parts.push_back(std::format("{} copper", cost.copper_bars));
        if (cost.iron_bars != 0u) parts.push_back(std::format("{} iron", cost.iron_bars));
        if (cost.gold_bars != 0u) parts.push_back(std::format("{} gold", cost.gold_bars));
        if (cost.diamonds != 0u) parts.push_back(std::format("{} diamond", cost.diamonds));
        if (cost.seeds != 0u) parts.push_back(std::format("{} seeds", cost.seeds));
        if (parts.empty()) return "free";

        std::string result = parts.front();
        for (std::size_t i = 1u; i < parts.size(); ++i) result += ", " + parts[i];
        return result;
    }

    template <typename ToolIconRect>
    std::array<ui::UpgradeCard, 2> build_menu_cards(
        const TerrainSculptTool& terrain_tool,
        const WaterTool&         water_tool,
        ToolIconRect&&           tool_icon_rect)
    {
        const auto tool_stats                            = terrain_tool.current_stats();
        const auto tool_next                             = terrain_tool.next_stats();
        const auto [next_tool_material, next_tool_level] = next_material_level(
            terrain_tool.material_index(), terrain_tool.level_index(), 2u);

        const std::vector<ui::UpgradeStat> tool_stats_rows =
                tool_next.has_value()
                    ? std::vector{
                        ui::UpgradeStat{
                            "Radius",
                            std::format("{:.2f}", tool_stats.radius),
                            std::format("{:.2f}", tool_next->radius)
                        },
                        ui::UpgradeStat{
                            "Speed",
                            std::format("{:.0f}/s", tool_stats.speed),
                            std::format("{:.0f}/s", tool_next->speed)
                        },
                        ui::UpgradeStat{
                            "Storage",
                            std::format("{}", tool_stats.capacity),
                            std::format("{}", tool_next->capacity)
                        }
                    }
                    : std::vector{
                        ui::UpgradeStat{
                            "Radius",
                            std::format("{:.2f}", tool_stats.radius), "Max"
                        },
                        ui::UpgradeStat{
                            "Speed",
                            std::format("{:.0f}/s", tool_stats.speed),
                            "Max"
                        },
                        ui::UpgradeStat{
                            "Storage",
                            std::format("{}", tool_stats.capacity), "Max"
                        }
                    };

        const auto bucket_stats = water_tool.current_stats();
        const auto bucket_next  = water_tool.next_stats();

        const auto [
            next_bucket_material,
            next_bucket_level
        ] = next_material_level(
            water_tool.material_index(),
            water_tool.level_index(),
            2u);

        const std::vector<ui::UpgradeStat> bucket_stats_rows =
                bucket_next.has_value()
                    ? std::vector{
                        ui::UpgradeStat{
                            "Capacity",
                            std::format("{}", bucket_stats.capacity),
                            std::format("{}", bucket_next->capacity)
                        }
                    }
                    : std::vector{
                        ui::UpgradeStat{
                            "Capacity",
                            std::format("{}", bucket_stats.capacity),
                            "Max"
                        }
                    };

        return {
            ui::UpgradeCard{
                .title         = "Digging Tool",
                .current_icon  = tool_icon_rect(terrain_tool.material_index(), 0u),
                .current_level = terrain_tool.level_index(),
                .next_icon     = tool_icon_rect(next_tool_material, 0u),
                .next_level    = next_tool_level,
                .stats         = tool_stats_rows,
                .cost_text     = format_cost(digging_upgrade_cost(terrain_tool)),
                .maxed         = terrain_tool.at_max_upgrade()
            },
            ui::UpgradeCard{
                .title         = "Bucket",
                .current_icon  = tool_icon_rect(water_tool.material_index(), 1u),
                .current_level = water_tool.level_index(),
                .next_icon     = tool_icon_rect(next_bucket_material, 1u),
                .next_level    = next_bucket_level,
                .stats         = bucket_stats_rows,
                .cost_text     = format_cost(bucket_upgrade_cost(water_tool)),
                .maxed         = water_tool.at_max_upgrade()
            }
        };
    }
}
