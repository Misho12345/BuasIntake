#pragma once

#include "pch.hpp"

#include "tools/SeedTool.hpp"
#include "tools/TerrainSculptTool.hpp"
#include "tools/TerrainTargetResolver.hpp"
#include "tools/TerrainToolHudRenderer.hpp"
#include "tools/WaterTool.hpp"
#include "ui/UpgradeMenu.hpp"

namespace game::tools
{
    class TerrainToolController final
    {
    public:
        TerrainToolController()  = default;
        ~TerrainToolController() = default;

        TerrainToolController(const TerrainToolController&)                = delete;
        TerrainToolController& operator=(const TerrainToolController&)     = delete;
        TerrainToolController(TerrainToolController&&) noexcept            = delete;
        TerrainToolController& operator=(TerrainToolController&&) noexcept = delete;

        // this owns all tool level routing so game code does not need to care which tool is active right now
        Result<void> initialize_ui_assets();

        void update(const TerrainToolContext& context, float dt);
        void handle_mouse_pressed(const TerrainToolContext& context, MouseButton button);
        void handle_scroll(float delta);

        void toggle_upgrade_menu();
        void close_upgrade_menu();

        // modal click handling lives here because the menu needs live tool and inventory state to decide what a click means
        void handle_upgrade_menu_click(
            const TerrainToolContext& context,
            vec2                      ui_position,
            uvec2                     target_size);

        void cancel_active_interaction();
        void cancel_bucket_placement();
        void destroy_graphics_resources();
        bool upgrade_menu_open() const { return upgrade_menu_open_; }

        // render code asks this for the cached water preview when the bucket is in placement mode
        std::optional<WaterTool::PreviewState> active_water_preview(const TerrainToolContext& context) const;

        void draw_ui(sf::RenderTarget& target) const;

    private:
        static constexpr std::size_t hotbar_slot_count{ 3u };

        // this avoids the classic close menu and instantly click the world on the same held mouse press problem
        bool should_suppress_world_input_after_modal(const TerrainToolContext& context);

        enum class HotbarSlot : std::uint8_t
        {
            Digging = 0,
            Water   = 1,
            Seeds   = 2
        };

        void select_slot(HotbarSlot slot);

        TerrainTool&       active_tool();
        const TerrainTool& active_tool() const;

        std::array<
            TerrainToolHudSlotData,
            hotbar_slot_count
        > build_hud_slots() const;

        TerrainToolHudSlotData build_digging_slot_data() const;
        TerrainToolHudSlotData build_water_slot_data() const;
        TerrainToolHudSlotData build_seed_slot_data() const;

        bool is_water_slot_selected() const;
        bool is_seed_slot_selected() const;

        sf::IntRect tool_icon_rect(std::size_t column, std::size_t row) const;

        TerrainTargetResolver target_resolver_{};
        TerrainSculptTool     terrain_tool_{};
        WaterTool             water_tool_{};
        SeedTool              seed_tool_{};

        ui::UpgradeMenu upgrade_menu_{};
        HotbarSlot      selected_slot_{ HotbarSlot::Digging };

        sf::Texture tools_texture_{};
        sf::Texture seed_icon_texture_{};
        sf::Font    ui_font_{};

        bool ui_assets_ready_{ false };
        bool upgrade_menu_open_{ false };
        bool require_fresh_mouse_press_after_modal_{ false };
    };
}
