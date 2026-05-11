#include "pch.hpp"

#include "tools/TerrainToolController.hpp"

#include "platform/InputSystem.hpp"
#include "resources/ResourceSystem.hpp"
#include "terrain/PlanetTerrain.hpp"
#include "tools/ToolUpgradeModel.hpp"

namespace game::tools
{
    Result<void> TerrainToolController::initialize_ui_assets()
    {
        if (ui_assets_ready_) return {};

        if (!tools_texture_.loadFromFile("assets/images/tools.png"))
        {
            return fail("Failed to load tools sprite sheet 'assets/images/tools.png'");
        }
        tools_texture_.setSmooth(false);

        if (!seed_icon_texture_.loadFromFile("assets/images/vegetation/ground_plants.png"))
        {
            return fail("Failed to load seed icon texture 'assets/images/vegetation/ground_plants.png'");
        }
        seed_icon_texture_.setSmooth(false);

        if (!ui_font_.openFromFile("assets/fonts/Cinzel-SemiBold.ttf"))
        {
            return fail("Failed to load font 'assets/fonts/Cinzel-SemiBold.ttf'");
        }

        ui_assets_ready_ = true;
        return {};
    }

    void TerrainToolController::update(const TerrainToolContext& context, const float dt)
    {
        if (context.terrain == nullptr) return;
        if (upgrade_menu_open_) return;
        if (should_suppress_world_input_after_modal(context)) return;

        active_tool().update(context, target_resolver_, dt);
    }

    void TerrainToolController::handle_mouse_pressed(const TerrainToolContext& context, const MouseButton button)
    {
        if (context.terrain == nullptr) return;
        if (upgrade_menu_open_) return;
        if (should_suppress_world_input_after_modal(context)) return;

        active_tool().handle_mouse_pressed(context, target_resolver_, button);
    }

    void TerrainToolController::handle_scroll(const float delta)
    {
        if (delta == 0.0f) return;
        if (upgrade_menu_open_) return;

        // The wheel adjusts placement amount while the bucket is armed; otherwise it cycles the hotbar.
        if (is_water_slot_selected() && water_tool_.is_placement_mode())
        {
            water_tool_.adjust_placement_amount(delta);
            return;
        }

        constexpr int slot_count = hotbar_slot_count;
        const int direction = delta > 0.0f ? 1 : -1;
        const int current = static_cast<int>(selected_slot_);
        select_slot(static_cast<HotbarSlot>((current + direction + slot_count) % slot_count));
    }

    void TerrainToolController::toggle_upgrade_menu()
    {
        upgrade_menu_open_ = !upgrade_menu_open_;
        cancel_active_interaction();
        require_fresh_mouse_press_after_modal_ = true;
    }

    void TerrainToolController::close_upgrade_menu()
    {
        if (upgrade_menu_open_) cancel_active_interaction();
        upgrade_menu_open_ = false;
        require_fresh_mouse_press_after_modal_ = true;
    }

    bool TerrainToolController::should_suppress_world_input_after_modal(const TerrainToolContext& context)
    {
        if (!require_fresh_mouse_press_after_modal_) return false;
        if (context.input == nullptr) return true;

        // Wait for a fresh mouse press so closing the menu on a held click does not immediately trigger a tool.
        const bool left_pressed = context.input->is_pressed(MouseButton::Left);
        const bool right_pressed = context.input->is_pressed(MouseButton::Right);
        if (!left_pressed && !right_pressed)
        {
            require_fresh_mouse_press_after_modal_ = false;
            return false;
        }

        if (context.input->just_pressed(MouseButton::Left) || context.input->just_pressed(MouseButton::Right))
        {
            require_fresh_mouse_press_after_modal_ = false;
            return false;
        }

        return true;
    }

    void TerrainToolController::handle_upgrade_menu_click(const TerrainToolContext& context,
                                                          const sf::Vector2f ui_position,
                                                          const sf::Vector2u target_size)
    {
        if (!upgrade_menu_open_ || context.resources == nullptr) return;

        if (ui::UpgradeMenu::button_rect(target_size, 0u).contains(ui_position) && !terrain_tool_.at_max_upgrade())
        {
            const auto cost = upgrade_model::digging_upgrade_cost(terrain_tool_);
            if (!context.resources->spend(cost)) return;
            terrain_tool_.upgrade();
            return;
        }

        if (ui::UpgradeMenu::button_rect(target_size, 1u).contains(ui_position) && !water_tool_.at_max_upgrade())
        {
            const auto cost = upgrade_model::bucket_upgrade_cost(water_tool_);
            if (!context.resources->spend(cost)) return;
            water_tool_.upgrade();
        }
    }

    void TerrainToolController::cancel_active_interaction()
    {
        terrain_tool_.deactivate();
        water_tool_.deactivate();
        seed_tool_.deactivate();
    }

    void TerrainToolController::cancel_bucket_placement()
    {
        water_tool_.cancel_placement();
    }

    void TerrainToolController::destroy_graphics_resources()
    {
        water_tool_.destroy_preview_resources();
        tools_texture_ = sf::Texture{};
        seed_icon_texture_ = sf::Texture{};
        ui_font_ = sf::Font{};
        ui_assets_ready_ = false;
    }

    std::optional<WaterTool::PreviewState> TerrainToolController::active_water_preview(const TerrainToolContext& context) const
    {
        if (upgrade_menu_open_) return std::nullopt;
        if (!is_water_slot_selected()) return std::nullopt;
        return water_tool_.preview_state(context, target_resolver_);
    }

    void TerrainToolController::draw_ui(sf::RenderTarget& target) const
    {
        if (!ui_assets_ready_) return;

        const auto slots = build_hud_slots();
        TerrainToolHudRenderer::draw(target, slots);
        if (upgrade_menu_open_)
        {
            upgrade_menu_.draw(
                target,
                ui_font_,
                tools_texture_,
                upgrade_model::build_menu_cards(
                    terrain_tool_,
                    water_tool_,
                    [this](const std::size_t column, const std::size_t row)
                    {
                        return tool_icon_rect(column, row);
                    }));
        }
    }

    void TerrainToolController::select_slot(const HotbarSlot slot)
    {
        if (slot == selected_slot_) return;

        active_tool().deactivate();
        selected_slot_ = slot;
        active_tool().activate();
    }

    TerrainTool& TerrainToolController::active_tool()
    {
        switch (selected_slot_)
        {
            case HotbarSlot::Digging:
                return terrain_tool_;
            case HotbarSlot::Water:
                return water_tool_;
            case HotbarSlot::Seeds:
                return seed_tool_;
        }

        return terrain_tool_;
    }

    const TerrainTool& TerrainToolController::active_tool() const
    {
        switch (selected_slot_)
        {
            case HotbarSlot::Digging:
                return terrain_tool_;
            case HotbarSlot::Water:
                return water_tool_;
            case HotbarSlot::Seeds:
                return seed_tool_;
        }

        return terrain_tool_;
    }

    std::array<TerrainToolHudSlotData, TerrainToolController::hotbar_slot_count> TerrainToolController::build_hud_slots() const
    {
        return {
            build_digging_slot_data(),
            build_water_slot_data(),
            build_seed_slot_data()
        };
    }

    TerrainToolHudSlotData TerrainToolController::build_digging_slot_data() const
    {
        const float fill_ratio = terrain_tool_.capacity() == 0u
                                     ? 0.0f
                                     : static_cast<float>(terrain_tool_.stored_ground()) / static_cast<float>(terrain_tool_.capacity());

        return {
            .texture = &tools_texture_,
            .icon_rect = tool_icon_rect(terrain_tool_.material_index(), 0u),
            .selected = selected_slot_ == HotbarSlot::Digging,
            .show_bar = true,
            .fill_ratio = fill_ratio,
            .overlay_ratio = std::nullopt,
            .bar_fill = 0xE0A14AFF_rgba,
            .bar_frame = 0xB97F3CF0_rgba,
            .bar_background = 0x2D1F16D2_rgba,
            .show_aim_ring = false
        };
    }

    TerrainToolHudSlotData TerrainToolController::build_water_slot_data() const
    {
        const float fill_ratio = water_tool_.current_capacity() == 0u ? 0.0f
                                                                      : static_cast<float>(water_tool_.current_amount()) /
                                                                            static_cast<float>(water_tool_.current_capacity());
        const std::optional<float> overlay_ratio = water_tool_.is_placement_mode() && water_tool_.current_capacity() > 0u
                                                       ? std::optional<float>{static_cast<float>(water_tool_.desired_place_amount()) /
                                                                              static_cast<float>(water_tool_.current_capacity())}
                                                       : std::nullopt;

        return {
            .texture = &tools_texture_,
            .icon_rect = tool_icon_rect(water_tool_.material_index(), water_tool_.current_amount() > 0u ? 2u : 1u),
            .selected = selected_slot_ == HotbarSlot::Water,
            .show_bar = true,
            .fill_ratio = fill_ratio,
            .overlay_ratio = overlay_ratio,
            .bar_fill = 0x4CBCEBFF_rgba,
            .bar_frame = 0x3F90CEF0_rgba,
            .bar_background = 0x13222CD2_rgba,
            .show_aim_ring = water_tool_.is_placement_mode()
        };
    }

    TerrainToolHudSlotData TerrainToolController::build_seed_slot_data() const
    {
        return {
            .texture = &seed_icon_texture_,
            .icon_rect = sf::IntRect{ { 7 * 32, 0 }, { 32, 32 } },
            .selected = selected_slot_ == HotbarSlot::Seeds,
            .show_bar = false,
            .fill_ratio = 0.0f,
            .overlay_ratio = std::nullopt,
            .bar_fill = sf::Color::Transparent,
            .bar_frame = sf::Color::Transparent,
            .bar_background = sf::Color::Transparent,
            .show_aim_ring = false
        };
    }

    bool TerrainToolController::is_water_slot_selected() const
    {
        return selected_slot_ == HotbarSlot::Water;
    }

    bool TerrainToolController::is_seed_slot_selected() const
    {
        return selected_slot_ == HotbarSlot::Seeds;
    }

    sf::IntRect TerrainToolController::tool_icon_rect(const std::size_t column, const std::size_t row) const
    {
        const auto texture_size = tools_texture_.getSize();
        const int cell_width = static_cast<int>(texture_size.x / 3u);
        const int cell_height = static_cast<int>(texture_size.y / 3u);
        return {
            {
                static_cast<int>(std::min<std::size_t>(column, 2u)) * cell_width,
                static_cast<int>(std::min<std::size_t>(row, 2u)) * cell_height
            },
            { cell_width, cell_height }
        };
    }
}
