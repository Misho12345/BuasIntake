#pragma once

#include "pch.hpp"

#include "resources/Inventory.hpp"
#include "resources/ResourceNode.hpp"

namespace game::resources
{
    static constexpr std::size_t hud_counter_count = inventory_item_count;

    struct HudCounter final
    {
        std::uint32_t count{ 0u };
        std::int32_t  feedback_amount{ 0 };
        float         feedback_alpha{ 0.0f };
        float         feedback_offset_y{ 0.0f };
    };

    struct HudState final
    {
        std::array<HudCounter, hud_counter_count> counters{};
    };

    // owns harvestable ore nodes and the player's resource inventory
    // terrain generation creates nodes here while tools and hud code consume the inventory-facing API
    class ResourceSystem final
    {
    public:
        const Inventory& inventory() const { return inventory_; }

        // this turns raw inventory plus transient feedback into something the hud can draw directly
        HudState                      hud_state() const;
        std::span<const ResourceNode> nodes() const { return nodes_; }
        std::uint64_t                 nodes_revision() const { return nodes_revision_; }

        bool has_at(ivec2 coord) const;

        // harvest_at finds the nearest node in range and applies both the inventory reward and the node removal in one place
        Result<void> harvest_at(vec2 world_position);

        // for the testers
        void grant_seeds(std::uint32_t amount);

        bool spend(const ResourceInventory& cost);
        void update(float dt);
        void validate() const;

        void clear_nodes();
        void reserve_nodes(std::size_t count);
        void add_node(const ResourceNode& node);

        // terrain edits use this to bulk remove resources whose support got deleted and sometimes still collect them for the player
        std::size_t remove_nodes(
            const std::unordered_set<std::uint64_t>& cleared_keys,
            bool                                     collect_removed = true);

        template <typename Predicate>
        std::size_t erase_nodes_if(Predicate&& predicate)
        {
            const auto removed = std::erase_if(
                nodes_,
                [this, &predicate](ResourceNode& node)
                {
                    if (!predicate(node)) return false;
                    occupied_node_keys_.erase(sample_key(node.coord));
                    return true;
                });

            if (removed > 0u) ++nodes_revision_;
            return removed;
        }

    private:
        struct CounterFeedback final
        {
            std::int32_t amount{ 0 };
            float        time_remaining{ 0.0f };
        };

        static constexpr float feedback_duration = 1.15f;

        void add_count(InventoryItem kind, std::int32_t amount);
        bool can_afford(const ResourceInventory& cost) const;
        void collect_kind(ResourceNodeKind kind);

        std::vector<ResourceNode>         nodes_{};
        std::unordered_set<std::uint64_t> occupied_node_keys_{};

        std::uint64_t nodes_revision_{ 0 };
        Inventory     inventory_{};

        std::array<CounterFeedback, inventory_item_count> feedback_{};
    };
}
