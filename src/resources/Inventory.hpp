#pragma once

#include "pch.hpp"

#include "resources/ResourceNode.hpp"

namespace game::resources
{
    enum class InventoryItem : std::uint8_t
    {
        Rock,
        CopperBar,
        IronBar,
        GoldBar,
        Diamond,
        Seeds,
        Count
    };

    inline constexpr std::size_t inventory_item_count = static_cast<std::size_t>(InventoryItem::Count);

    constexpr std::size_t inventory_item_index(const InventoryItem item) { return static_cast<std::size_t>(item); }

    struct ResourceInventory final
    {
        std::uint32_t rocks{ 0u };
        std::uint32_t iron_bars{ 0u };
        std::uint32_t copper_bars{ 0u };
        std::uint32_t gold_bars{ 0u };
        std::uint32_t diamonds{ 0u };
        std::uint32_t seeds{ 0u };
    };

    struct ResourceInventoryEntry final
    {
        InventoryItem   item{ InventoryItem::Rock };
        std::uint32_t ResourceInventory::* amount{ nullptr };
        std::string_view label{};
    };

    inline constexpr std::array resource_inventory_entries{
        ResourceInventoryEntry{ InventoryItem::Rock, &ResourceInventory::rocks, "rock" },
        ResourceInventoryEntry{ InventoryItem::CopperBar, &ResourceInventory::copper_bars, "copper" },
        ResourceInventoryEntry{ InventoryItem::IronBar, &ResourceInventory::iron_bars, "iron" },
        ResourceInventoryEntry{ InventoryItem::GoldBar, &ResourceInventory::gold_bars, "gold" },
        ResourceInventoryEntry{ InventoryItem::Diamond, &ResourceInventory::diamonds, "diamond" },
        ResourceInventoryEntry{ InventoryItem::Seeds, &ResourceInventory::seeds, "seeds" }
    };

    struct InventoryCost final
    {
        std::array<std::uint32_t, inventory_item_count> amounts{};
    };

    struct ResourceReward final
    {
        InventoryItem item{ InventoryItem::Rock };
        std::uint32_t amount{ 1u };
    };

    class Inventory final
    {
    public:
        std::uint32_t count(const InventoryItem item) const { return counts_[inventory_item_index(item)]; }

        void add(const InventoryItem item, const std::uint32_t amount)
        {
            counts_[inventory_item_index(item)] += amount;
        }

        std::uint32_t remove_up_to(const InventoryItem item, const std::uint32_t requested)
        {
            auto&      value   = counts_[inventory_item_index(item)];
            const auto removed = std::min(value, requested);
            value              -= removed;
            return removed;
        }

        bool can_afford(const InventoryCost& cost) const
        {
            for (std::size_t i = 0; i < cost.amounts.size(); ++i)
            {
                if (counts_[i] < cost.amounts[i]) return false;
            }

            return true;
        }

    private:
        std::array<std::uint32_t, inventory_item_count> counts_{};
    };

    constexpr InventoryCost make_inventory_cost(const ResourceInventory& cost)
    {
        InventoryCost result{};
        for (const auto& entry : resource_inventory_entries)
        {
            result.amounts[inventory_item_index(entry.item)] = cost.*(entry.amount);
        }

        return result;
    }

    constexpr ResourceReward reward_for(const ResourceNodeKind kind)
    {
        switch (kind)
        {
            case ResourceNodeKind::Rock: return { InventoryItem::Rock, 1u };
            case ResourceNodeKind::IronOre: return { InventoryItem::IronBar, 1u };
            case ResourceNodeKind::CopperOre: return { InventoryItem::CopperBar, 1u };
            case ResourceNodeKind::GoldOre: return { InventoryItem::GoldBar, 1u };
            case ResourceNodeKind::DiamondOre: return { InventoryItem::Diamond, 1u };
            case ResourceNodeKind::DeadPlant: return { InventoryItem::Seeds, 2u };
        }

        return { InventoryItem::Rock, 0u };
    }

}
