#pragma once

#include "pch.hpp"

#include "resources/Inventory.hpp"
#include "resources/ResourceNode.hpp"

namespace game::resources
{
    enum class HudKind : std::uint8_t
    {
        Rock,
        CopperBar,
        IronBar,
        GoldBar,
        Diamond,
        Seeds,
        Count
    };

    static constexpr std::size_t hud_counter_count = static_cast<std::size_t>(HudKind::Count);

    struct HudCounter final
    {
        std::uint32_t count{0u};
        std::int32_t feedback_amount{0};
        float feedback_alpha{0.0f};
        float feedback_offset_y{0.0f};
    };

    struct HudState final
    {
        std::array<HudCounter, hud_counter_count> counters{};
    };

    class ResourceSystem final
    {
      public:
        const Inventory& inventory() const noexcept
        {
            return inventory_;
        }
        HudState hud_state() const noexcept;
        std::span<const ResourceNode> nodes() const noexcept
        {
            return nodes_;
        }
        std::uint64_t nodes_revision() const noexcept
        {
            return nodes_revision_;
        }

        bool has_at(ivec2 coord) const;
        Result<void> harvest_at(vec2 world_position);
        bool can_afford(const ResourceInventory& cost) const noexcept;
        bool spend(const ResourceInventory& cost);
        void grant(const ResourceInventory& reward);
        void grant_seeds(std::uint32_t amount);
        void update(float dt);
        void validate() const;

        void clear_nodes();
        void reserve_nodes(std::size_t count);
        void add_node(const ResourceNode& node);
        std::size_t remove_nodes(const std::unordered_set<std::uint64_t>& cleared_keys, bool collect_removed = true);
        template <typename Predicate> std::size_t erase_nodes_if(Predicate&& predicate)
        {
            const auto removed = std::erase_if(nodes_,
                                               [this, &predicate](ResourceNode& node)
                                               {
                                                   if (!predicate(node))
                                                       return false;
                                                   occupied_node_keys_.erase(sample_key(node.coord));
                                                   return true;
                                               });

            if (removed > 0u)
                ++nodes_revision_;
            return removed;
        }

      private:
        struct CounterFeedback final
        {
            std::int32_t amount{0};
            float time_remaining{0.0f};
        };

        static constexpr float feedback_duration = 1.15f;

        static std::uint64_t sample_key(ivec2 coord) noexcept;
        void add_count(InventoryItem kind, std::int32_t amount);
        void collect_kind(ResourceNodeKind kind);

        std::vector<ResourceNode> nodes_{};
        std::unordered_set<std::uint64_t> occupied_node_keys_{};
        std::uint64_t nodes_revision_{0};
        Inventory inventory_{};
        std::array<CounterFeedback, inventory_item_count> feedback_{};
    };
}
