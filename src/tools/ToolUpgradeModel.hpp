#pragma once

#include "pch.hpp"

#include "resources/Inventory.hpp"
#include "tools/TerrainSculptTool.hpp"
#include "tools/ToolProgression.hpp"
#include "tools/WaterTool.hpp"
#include "ui/UpgradeMenu.hpp"

namespace game::tools::upgrade_model
{
    resources::ResourceInventory digging_upgrade_cost(const TerrainSculptTool& tool);
    resources::ResourceInventory bucket_upgrade_cost(const WaterTool& tool);
    std::string format_cost(const resources::ResourceInventory& cost);

    template <typename ToolIconRect>
    std::array<ui::UpgradeCard, 2> build_menu_cards(
        const TerrainSculptTool& terrain_tool,
        const WaterTool&         water_tool,
        ToolIconRect&&           tool_icon_rect)
    {
        const auto tool_stats                            = terrain_tool.current_stats();
        const auto tool_next                             = terrain_tool.next_stats();
        const auto [next_tool_material, next_tool_level] = next_tool_material_level(
            terrain_tool.material_index(), terrain_tool.level_index());
        const bool tool_maxed = !tool_next.has_value();

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
        const bool bucket_maxed = !bucket_next.has_value();

        const auto [
            next_bucket_material,
            next_bucket_level
        ] = next_tool_material_level(
            water_tool.material_index(),
            water_tool.level_index());

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
                .next_icon     = tool_icon_rect(tool_maxed ? terrain_tool.material_index() : next_tool_material, 0u),
                .next_level    = tool_maxed ? terrain_tool.level_index() : next_tool_level,
                .stats         = tool_stats_rows,
                .cost_text     = format_cost(digging_upgrade_cost(terrain_tool)),
                .maxed         = tool_maxed
            },
            ui::UpgradeCard{
                .title         = "Bucket",
                .current_icon  = tool_icon_rect(water_tool.material_index(), 1u),
                .current_level = water_tool.level_index(),
                .next_icon     = tool_icon_rect(bucket_maxed ? water_tool.material_index() : next_bucket_material, 1u),
                .next_level    = bucket_maxed ? water_tool.level_index() : next_bucket_level,
                .stats         = bucket_stats_rows,
                .cost_text     = format_cost(bucket_upgrade_cost(water_tool)),
                .maxed         = bucket_maxed
            }
        };
    }
}
