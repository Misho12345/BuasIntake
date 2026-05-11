#pragma once

#include "pch.hpp"

#include "tools/ToolStrategy.hpp"
#include "water/WaterSystem.hpp"

namespace game::tools
{
    class WaterTool final : public TerrainTool
    {
    public:
        WaterTool() = default;
        ~WaterTool() override = default;

        void deactivate() override;
        void update(const TerrainToolContext& context, const TerrainTargetResolver& resolver, float dt) override;
        void handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver, MouseButton button) override;

        void adjust_placement_amount(float delta);
        void upgrade();
        void cancel_placement();
        void destroy_preview_resources();

        struct PreviewState final
        {
            const water::WaterPreviewMesh* preview{ nullptr };
            std::uint64_t revision{ 0u };
        };

        std::optional<PreviewState> preview_state(const TerrainToolContext& context, const TerrainTargetResolver& resolver) const;

        struct BucketStats final
        {
            std::uint32_t capacity{ 0u };
        };

        bool is_placement_mode() const;
        std::size_t material_index() const;
        std::size_t level_index() const;
        std::string_view material_name() const;
        bool at_max_upgrade() const;
        BucketStats current_stats() const;
        std::optional<BucketStats> next_stats() const;
        std::uint32_t current_amount() const;
        std::uint32_t current_capacity() const;
        std::uint32_t desired_place_amount() const;

    private:
        struct BucketTier final
        {
            std::uint32_t capacity{ 0u };
        };

        struct PreviewCache final
        {
            bool valid{ false };
            bool has_preview{ false };
            vec2 target_world{ 0.0f, 0.0f };
            std::uint32_t amount{ 0u };
            std::uint64_t terrain_revision{ 0u };
            std::uint64_t water_revision{ 0u };
            water::WaterPreviewMesh preview{};
        };

        void begin_placement();
        void confirm_placement(const TerrainToolContext& context, const TerrainTargetResolver& resolver);
        void collect_water(const TerrainToolContext& context, const TerrainTargetResolver& resolver);
        void refresh_preview_cache(const TerrainToolContext& context, const TerrainTargetResolver& resolver) const;
        void invalidate_preview_cache() const;
        BucketTier current_tier() const;
        std::optional<BucketTier> next_tier() const;
        std::size_t flat_tier_index() const;

        std::size_t material_index_{ 0u };
        std::size_t level_index_{ 0u };
        std::uint32_t current_amount_{ 24u };
        bool placement_mode_{ false };
        std::uint32_t desired_place_amount_{ 0u };
        mutable PreviewCache preview_cache_{};
        mutable std::uint64_t preview_revision_{ 0u };
    };
}
