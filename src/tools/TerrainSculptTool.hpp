#pragma once

#include "pch.hpp"

#include "tools/TerrainTool.hpp"

namespace game::tools
{
    // dig and place tool for terrain construction
    class TerrainSculptTool final : public TerrainTool
    {
    public:
        TerrainSculptTool()           = default;
        ~TerrainSculptTool() override = default;

        void deactivate() override;

        void update(
            const TerrainToolContext&    context,
            const TerrainTargetResolver& resolver,
            float                        dt) override;

        void handle_mouse_pressed(
            const TerrainToolContext&    context,
            const TerrainTargetResolver& resolver,
            MouseButton                  button) override;

        // upgrade walks through the material x level ladder and the current tier data drives radius speed and storage
        void upgrade();

        struct ToolStats final
        {
            float         radius{ 0.0f };
            float         speed{ 0.0f };
            std::uint32_t capacity{ 0u };
        };

        std::size_t              material_index() const;
        std::size_t              level_index() const;
        bool                     at_max_upgrade() const;
        ToolStats                current_stats() const;
        std::optional<ToolStats> next_stats() const;
        std::uint32_t            stored_ground() const;
        std::uint32_t            capacity() const;

    private:
        struct BrushConfig final
        {
            float radius{ 1.0f };
            float signed_strength_per_stamp{ 0.0f };
            float stamps_per_second{ 1.0f };
            float spacing_factor{ 1.0f };
            float falloff_exponent{ 1.8f };
        };

        struct ToolTier final
        {
            BrushConfig   dig{};
            BrushConfig   place{};
            std::uint32_t capacity{ 0u };
        };

        struct BrushStroke final
        {
            bool                active{ false };
            float               emission_accumulator{ 0.0f };
            std::optional<vec2> last_stamp_world{ std::nullopt };

            void begin();
            void reset();
        };

        const ToolTier& current_tier() const;
        static const ToolTier& terrain_tool_tier(std::size_t tier_index);
        std::size_t     flat_tier_index() const;

        // this is the actual sculpting loop and it is used for both digging and placing
        // it turns a held mouse button plus movement over time into a stream of brush edits with spacing and capacity rules
        void emit_brush_stamps(
            const TerrainToolContext&    context,
            const TerrainTargetResolver& resolver,
            MouseButton                  button,
            bool                         digging,
            const BrushConfig&           config,
            BrushStroke&                 state,
            float                        dt);

        std::size_t   material_index_{ 0u };
        std::size_t   level_index_{ 0u };
        std::uint32_t stored_ground_{ 0u };
        float         placement_lift_cooldown_{ 0.0f };
        bool          suppress_left_stroke_until_released_{ false };
        BrushStroke   dig_state_{};
        BrushStroke   place_state_{};
    };
}
