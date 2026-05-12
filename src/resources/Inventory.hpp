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

    struct InventoryCost final
    {
        std::array<std::uint32_t, inventory_item_count> amounts{};

        std::uint32_t count(const InventoryItem item) const { return amounts[inventory_item_index(item)]; }
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

        bool spend(const InventoryCost& cost)
        {
            if (!can_afford(cost)) return false;
            for (std::size_t i = 0; i < cost.amounts.size(); ++i)
            {
                counts_[i] -= cost.amounts[i];
            }

            return true;
        }

    private:
        std::array<std::uint32_t, inventory_item_count> counts_{};
    };

    constexpr InventoryCost make_inventory_cost(const ResourceInventory& cost)
    {
        return InventoryCost{
            .amounts = { cost.rocks, cost.copper_bars, cost.iron_bars, cost.gold_bars, cost.diamonds, cost.seeds }
        };
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

    inline constexpr std::array hud_inventory_order{
        InventoryItem::Rock,
        InventoryItem::CopperBar,
        InventoryItem::IronBar,
        InventoryItem::GoldBar,
        InventoryItem::Diamond,
        InventoryItem::Seeds
    };
}
