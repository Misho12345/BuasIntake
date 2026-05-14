#include "pch.hpp"

#include "resources/ResourceSystem.hpp"

namespace game::resources
{
    HudState ResourceSystem::hud_state() const
    {
        HudState state{};

        auto assign_counter = [&](const InventoryItem item)
        {
            auto& [
                count,
                feedback_amount,
                feedback_alpha,
                feedback_offset_y
            ] = state.counters[inventory_item_index(item)];

            count = inventory_.count(item);

            const auto& [amount, time_remaining] = feedback_[inventory_item_index(item)];
            if (amount == 0 || time_remaining <= 0.0f) return;

            const float elapsed      = feedback_duration - time_remaining;

            const float alpha_factor = std::clamp(
                elapsed / 0.12f, 0.0f, 1.0f) * std::clamp(
                time_remaining / 0.45f,
                0.0f, 1.0f);

            feedback_amount   = amount;
            feedback_alpha    = std::clamp(alpha_factor, 0.0f, 1.0f);
            feedback_offset_y = elapsed * 8.0f;
        };

        for (const auto& entry : resource_inventory_entries)
        {
            assign_counter(entry.item);
        }

        return state;
    }

    bool ResourceSystem::has_at(const ivec2 coord) const { return occupied_node_keys_.contains(sample_key(coord)); }

    Result<void> ResourceSystem::harvest_at(const vec2 world_position)
    {
        if (nodes_.empty()) return fail("No harvestable resources are available");

        static constexpr float max_harvest_distance = 1.15f;
        const float            max_distance_sq      = max_harvest_distance * max_harvest_distance;

        std::optional<std::size_t> best_index;
        float                      best_distance_sq = max_distance_sq;

        for (std::size_t i = 0; i < nodes_.size(); ++i)
        {
            const auto& resource    = nodes_[i];
            const vec2  delta       = resource.anchor_world - world_position;
            const float distance_sq = delta.lengthSquared();
            if (distance_sq > best_distance_sq) continue;

            best_distance_sq = distance_sq;
            best_index       = i;
        }

        if (!best_index.has_value())
        {
            return fail("No harvestable resource is close enough to collect");
        }

        collect_kind(nodes_[*best_index].kind);
        occupied_node_keys_.erase(sample_key(nodes_[*best_index].coord));
        nodes_.erase(nodes_.begin() + static_cast<std::ptrdiff_t>(*best_index));
        ++nodes_revision_;
        return {};
    }

    bool ResourceSystem::can_afford(const ResourceInventory& cost) const
    {
        return inventory_.can_afford(make_inventory_cost(cost));
    }

    void ResourceSystem::grant_seeds(const std::uint32_t amount)
    {
        add_count(InventoryItem::Seeds, static_cast<std::int32_t>(amount));
    }

    bool ResourceSystem::spend(const ResourceInventory& cost)
    {
        if (!can_afford(cost)) return false;

        for (const auto& entry : resource_inventory_entries)
        {
            add_count(entry.item, -static_cast<std::int32_t>(cost.*(entry.amount)));
        }

        return true;
    }

    void ResourceSystem::update(const float dt)
    {
        for (auto& entry : feedback_)
        {
            if (entry.time_remaining <= 0.0f) continue;
            entry.time_remaining = std::max(0.0f, entry.time_remaining - dt);
            if (entry.time_remaining <= 0.0f) entry.amount = 0;
        }
    }

    void ResourceSystem::validate() const
    {
        #ifndef NDEBUG
        std::unordered_set<std::uint64_t> seen_nodes;
        seen_nodes.reserve(nodes_.size());
        for (const auto& node : nodes_)
        {
            const auto key = game::sample_key(node.coord);
            if (!seen_nodes.insert(key).second)
            {
                Log::error("ResourceSystem validation failed: duplicate node at ({}, {})", node.coord.x, node.coord.y);
            }
        }
        #endif
    }

    void ResourceSystem::clear_nodes()
    {
        if (!nodes_.empty()) ++nodes_revision_;
        nodes_.clear();
        occupied_node_keys_.clear();
    }

    void ResourceSystem::reserve_nodes(const std::size_t count)
    {
        nodes_.reserve(count);
        occupied_node_keys_.reserve(count);
    }

    void ResourceSystem::add_node(const ResourceNode& node)
    {
        if (!occupied_node_keys_.insert(sample_key(node.coord)).second) return;

        nodes_.push_back(node);
        ++nodes_revision_;
    }

    std::size_t ResourceSystem::remove_nodes(
        const std::unordered_set<std::uint64_t>& cleared_keys,
        const bool                               collect_removed)
    {
        if (cleared_keys.empty()) return 0u;

        const auto removed = std::erase_if(
            nodes_,
            [this, &cleared_keys, collect_removed](const ResourceNode& node)
            {
                if (!cleared_keys.contains(sample_key(node.coord))) return false;
                occupied_node_keys_.erase(sample_key(node.coord));
                if (collect_removed) collect_kind(node.kind);
                return true;
            });

        if (removed > 0u) ++nodes_revision_;

        return removed;
    }

    void ResourceSystem::add_count(const InventoryItem kind, const std::int32_t amount)
    {
        if (amount == 0) return;

        std::int32_t applied_amount = 0;
        if (amount > 0)
        {
            inventory_.add(kind, static_cast<std::uint32_t>(amount));
            applied_amount = amount;
        }
        else
        {
            const auto removed = inventory_.remove_up_to(kind, static_cast<std::uint32_t>(-amount));
            applied_amount     = -static_cast<std::int32_t>(removed);
        }

        if (applied_amount == 0) return;

        auto& entry = feedback_[inventory_item_index(kind)];

        if ((entry.amount > 0 && applied_amount > 0) ||
            (entry.amount < 0 && applied_amount < 0))
            entry.amount  += applied_amount;
        else entry.amount = applied_amount;

        entry.time_remaining = feedback_duration;
    }

    void ResourceSystem::collect_kind(const ResourceNodeKind kind)
    {
        const auto [item, amount] = reward_for(kind);
        add_count(item, static_cast<std::int32_t>(amount));
    }
}
