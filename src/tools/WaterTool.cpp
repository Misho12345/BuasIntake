#include "pch.hpp"

#include "tools/WaterTool.hpp"

#include "terrain/PlanetTerrain.hpp"
#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
    namespace
    {
        constexpr std::array<std::uint32_t, 9> bucket_capacities{{
            24u, 32u, 40u,
            52u, 64u, 76u,
            92u, 112u, 132u
        }};
    }

    void WaterTool::deactivate()
    {
        cancel_placement();
    }

    void WaterTool::update(const TerrainToolContext& /*context*/, const TerrainTargetResolver& /*resolver*/, const float /*dt*/)
    {
    }

    void WaterTool::handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver, const MouseButton button)
    {
        if (context.terrain == nullptr) return;

        if (button == MouseButton::Right)
        {
            if (!placement_mode_) begin_placement();
            else confirm_placement(context, resolver);
            return;
        }

        if (button != MouseButton::Left) return;

        if (placement_mode_)
        {
            cancel_placement();
            return;
        }

        collect_water(context, resolver);
    }

    void WaterTool::begin_placement()
    {
        if (current_amount_ == 0u) return;

        placement_mode_ = true;
        desired_place_amount_ = current_amount_;
        invalidate_preview_cache();
    }

    void WaterTool::confirm_placement(const TerrainToolContext& context, const TerrainTargetResolver& resolver)
    {
        if (context.terrain == nullptr || context.water == nullptr) return;

        if (current_amount_ == 0u || desired_place_amount_ == 0u)
        {
            cancel_placement();
            return;
        }

        const auto world_position = resolver.water_placement_target_world_position(context);
        if (!world_position.has_value()) return;

        const auto placed = context.water->place_water(*context.terrain, *world_position, std::min(desired_place_amount_, current_amount_));
        if (!placed) return;
        if (placed->status != water::WaterActionStatus::Applied || placed->changed_units == 0u) return;

        current_amount_ -= std::min(current_amount_, placed->changed_units);
        invalidate_preview_cache();
        cancel_placement();
    }

    void WaterTool::collect_water(const TerrainToolContext& context, const TerrainTargetResolver& resolver)
    {
        if (context.terrain == nullptr || context.water == nullptr) return;

        const auto free_space = current_capacity() - std::min(current_amount_, current_capacity());
        if (free_space == 0u) return;

        const auto world_position = resolver.water_pickup_target_world_position(context);
        if (!world_position.has_value()) return;

        const auto picked_up = context.water->pickup_water(*context.terrain, *world_position, free_space);
        if (!picked_up) return;
        if (picked_up->status != water::WaterActionStatus::Applied || picked_up->changed_units == 0u) return;

        current_amount_ = std::min(current_capacity(), current_amount_ + picked_up->changed_units);
        invalidate_preview_cache();
    }

    void WaterTool::adjust_placement_amount(const float delta)
    {
        if (!placement_mode_ || delta == 0.0f) return;

        const int direction = delta > 0.0f ? 1 : -1;
        const int next_value = std::clamp(static_cast<int>(desired_place_amount_) + direction, 0, static_cast<int>(current_amount_));
        desired_place_amount_ = static_cast<std::uint32_t>(next_value);
        invalidate_preview_cache();
    }

    void WaterTool::upgrade()
    {
        if (at_max_upgrade()) return;

        if (level_index_ + 1u < 3u)
        {
            ++level_index_;
        }
        else
        {
            ++material_index_;
            level_index_ = 0u;
        }

        current_amount_ = std::min(current_amount_, current_capacity());
        invalidate_preview_cache();
    }

    void WaterTool::cancel_placement()
    {
        placement_mode_ = false;
        desired_place_amount_ = 0u;
        invalidate_preview_cache();
    }

    void WaterTool::destroy_preview_resources()
    {
        preview_cache_ = {};
        ++preview_revision_;
    }

    std::optional<WaterTool::PreviewState> WaterTool::preview_state(const TerrainToolContext& context,
                                                                    const TerrainTargetResolver& resolver) const
    {
        refresh_preview_cache(context, resolver);
        if (!preview_cache_.has_preview) return std::nullopt;

        return PreviewState{
            .preview = &preview_cache_.preview,
            .revision = preview_revision_
        };
    }

    void WaterTool::refresh_preview_cache(const TerrainToolContext& context, const TerrainTargetResolver& resolver) const
    {
        if (!placement_mode_ || context.terrain == nullptr || context.water == nullptr || desired_place_amount_ == 0u)
        {
            preview_cache_ = {};
            return;
        }

        const auto target_position = resolver.water_placement_target_world_position(context);
        if (!target_position.has_value())
        {
            preview_cache_ = {};
            return;
        }

        const float cell_extent = std::min(context.terrain->terrain_cell_size().x, context.terrain->terrain_cell_size().y);
        const bool same_target = preview_cache_.valid
            && (preview_cache_.target_world - *target_position).lengthSquared() <= (cell_extent * 0.35f) * (cell_extent * 0.35f);
        const bool same_amount = preview_cache_.valid && preview_cache_.amount == desired_place_amount_;
        const bool same_terrain = preview_cache_.valid && preview_cache_.terrain_revision == context.terrain->field_revision();
        const bool same_water = preview_cache_.valid && preview_cache_.water_revision == context.terrain->water_revision();
        // The preview is revision-driven so scrolling or terrain edits only rebuild it when the inputs really changed.
        if (!same_target || !same_amount || !same_terrain || !same_water)
        {
            const auto preview = context.water->build_preview(*context.terrain, *target_position, desired_place_amount_);
            ++preview_revision_;
            preview_cache_.valid = true;
            preview_cache_.target_world = *target_position;
            preview_cache_.amount = desired_place_amount_;
            preview_cache_.terrain_revision = context.terrain->field_revision();
            preview_cache_.water_revision = context.terrain->water_revision();
            preview_cache_.has_preview = false;

            if (!preview)
            {
                preview_cache_.valid = false;
                return;
            }

            if (!preview->has_value()) return;

            preview_cache_.preview = std::move(**preview);
            preview_cache_.has_preview = true;
        }
    }

    bool WaterTool::is_placement_mode() const
    {
        return placement_mode_;
    }

    std::size_t WaterTool::material_index() const
    {
        return material_index_;
    }

    std::size_t WaterTool::level_index() const
    {
        return level_index_;
    }

    std::string_view WaterTool::material_name() const
    {
        static constexpr std::array names{ "Wood", "Copper", "Iron" };
        return names[std::min(material_index_, names.size() - 1u)];
    }

    bool WaterTool::at_max_upgrade() const
    {
        return material_index_ >= 2u && level_index_ >= 2u;
    }

    WaterTool::BucketStats WaterTool::current_stats() const
    {
        return { .capacity = current_capacity() };
    }

    std::optional<WaterTool::BucketStats> WaterTool::next_stats() const
    {
        const auto tier = next_tier();
        if (!tier.has_value()) return std::nullopt;
        return BucketStats{ .capacity = tier->capacity };
    }

    std::uint32_t WaterTool::current_amount() const
    {
        return current_amount_;
    }

    std::uint32_t WaterTool::current_capacity() const
    {
        return current_tier().capacity;
    }

    std::uint32_t WaterTool::desired_place_amount() const
    {
        return desired_place_amount_;
    }

    void WaterTool::invalidate_preview_cache() const
    {
        preview_cache_ = {};
        ++preview_revision_;
    }

    WaterTool::BucketTier WaterTool::current_tier() const
    {
        return BucketTier{
            .capacity = bucket_capacities[std::min(flat_tier_index(), bucket_capacities.size() - 1u)]
        };
    }

    std::optional<WaterTool::BucketTier> WaterTool::next_tier() const
    {
        if (at_max_upgrade()) return std::nullopt;

        std::size_t next_material = material_index_;
        std::size_t next_level = level_index_ + 1u;
        if (next_level >= 3u)
        {
            next_level = 0u;
            ++next_material;
        }

        const auto next_index = std::min<std::size_t>(next_material, 2u) * 3u + std::min<std::size_t>(next_level, 2u);
        return BucketTier{
            .capacity = bucket_capacities[std::min(next_index, bucket_capacities.size() - 1u)]
        };
    }

    std::size_t WaterTool::flat_tier_index() const
    {
        return std::min<std::size_t>(material_index_, 2u) * 3u + std::min<std::size_t>(level_index_, 2u);
    }
}
