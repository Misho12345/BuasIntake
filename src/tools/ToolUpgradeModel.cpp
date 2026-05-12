#include "pch.hpp"

#include "tools/ToolUpgradeModel.hpp"

namespace game::tools::upgrade_model
{
    resources::ResourceInventory digging_upgrade_cost(const TerrainSculptTool& tool)
    {
        resources::ResourceInventory cost{};
        if (tool.at_max_upgrade()) return cost;

        auto [next_material, next_level] = next_tool_material_level(tool.material_index(), tool.level_index());

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

    resources::ResourceInventory bucket_upgrade_cost(const WaterTool& tool)
    {
        resources::ResourceInventory cost{};
        if (tool.at_max_upgrade()) return cost;

        auto [next_material, next_level] = next_tool_material_level(tool.material_index(), tool.level_index());

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

    std::string format_cost(const resources::ResourceInventory& cost)
    {
        std::vector<std::string> parts;
        for (const auto& entry : resources::resource_inventory_entries)
        {
            const auto amount = cost.*(entry.amount);
            if (amount != 0u) parts.push_back(std::format("{} {}", amount, entry.label));
        }
        if (parts.empty()) return "free";

        std::string result = parts.front();
        for (std::size_t i = 1u; i < parts.size(); ++i) result += ", " + parts[i];
        return result;
    }
}
