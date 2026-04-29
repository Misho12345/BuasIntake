#include "pch.hpp"
#include "tools/TerrainToolController.hpp"

namespace game::tools
{
	void TerrainToolController::initialize_ui_assets()
	{
		if (ui_assets_ready_) return;

		if (!tools_texture_.loadFromFile("assets/images/tools.png"))
		{
			throw std::runtime_error("Failed to load tools sprite sheet");
		}
		tools_texture_.setSmooth(false);

		if (!seed_icon_texture_.loadFromFile("assets/images/vegetation/ground_plants.png"))
		{
			throw std::runtime_error("Failed to load seed icon texture");
		}
		seed_icon_texture_.setSmooth(false);

		ui_assets_ready_ = true;
	}

	void TerrainToolController::update(const TerrainToolContext& context, const float dt)
	{
		if (context.terrain == nullptr) return;

		sync_active_tool();
		active_tool_->update(context, target_resolver_, dt);
	}

	void TerrainToolController::handle_mouse_pressed(const TerrainToolContext& context, const MouseButton button)
	{
		if (context.terrain == nullptr) return;

		sync_active_tool();
		active_tool_->handle_mouse_pressed(context, target_resolver_, button);
	}

	void TerrainToolController::handle_scroll(const float delta)
	{
		if (delta == 0.0f) return;

		if (is_water_slot_selected() && water_tool_.is_placement_mode())
		{
			water_tool_.adjust_placement_amount(delta);
			return;
		}

		const int slot_count = static_cast<int>(hotbar_slot_count);
		const int direction = delta > 0.0f ? 1 : -1;
		const int current = static_cast<int>(selected_slot_);
		selected_slot_ = static_cast<HotbarSlot>((current + direction + slot_count) % slot_count);
		sync_active_tool();
	}

	void TerrainToolController::handle_upgrade()
	{
		if (is_water_slot_selected()) water_tool_.upgrade();
		else if (!is_seed_slot_selected()) terrain_tool_.upgrade();
	}

	void TerrainToolController::handle_zero_shortcut()
	{
		if (is_water_slot_selected()) water_tool_.fill_to_capacity();
		else if (!is_seed_slot_selected()) terrain_tool_.clear_storage();
	}

	void TerrainToolController::cancel_bucket_placement()
	{
		water_tool_.cancel_placement();
	}

	void TerrainToolController::draw_world_preview(const TerrainToolContext& context, const sf::View& view) const
	{
		if (!is_water_slot_selected()) return;
		water_tool_.draw_world_preview(context, target_resolver_, view);
	}

	void TerrainToolController::draw_ui(sf::RenderTarget& target) const
	{
		if (!ui_assets_ready_) return;

		const auto slots = build_hud_slots();
		TerrainToolHudRenderer::draw(target, slots);
	}

	void TerrainToolController::export_current_chunk_field(const TerrainToolContext& context) const
	{
		if (context.terrain == nullptr) return;

		static_cast<void>(context.terrain->save_chunk_field_image(context.player_world_position));
	}

	void TerrainToolController::sync_active_tool()
	{
		IToolStrategy* next_tool = &terrain_tool_;
		if (is_water_slot_selected()) next_tool = &water_tool_;
		else if (is_seed_slot_selected()) next_tool = &seed_tool_;
		if (next_tool == active_tool_) return;

		active_tool_->deactivate();
		active_tool_ = next_tool;
		active_tool_->activate();
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
		const float fill_ratio = terrain_tool_.capacity() == 0u ? 0.0f :
			static_cast<float>(terrain_tool_.stored_ground()) / static_cast<float>(terrain_tool_.capacity());

		return {
			.texture = &tools_texture_,
			.icon_rect = tool_icon_rect(terrain_tool_.tier_index(), 0u),
			.selected = selected_slot_ == HotbarSlot::Digging,
			.show_bar = true,
			.fill_ratio = fill_ratio,
			.overlay_ratio = std::nullopt,
			.bar_fill = sf::Color(224, 161, 74, 255),
			.bar_frame = sf::Color(185, 127, 60, 240),
			.bar_background = sf::Color(45, 31, 22, 210),
			.show_aim_ring = false
		};
	}

	TerrainToolHudSlotData TerrainToolController::build_water_slot_data() const
	{
		const float fill_ratio = water_tool_.current_capacity() == 0u ? 0.0f :
			static_cast<float>(water_tool_.current_amount()) / static_cast<float>(water_tool_.current_capacity());
		const std::optional<float> overlay_ratio =
			water_tool_.is_placement_mode() && water_tool_.current_capacity() > 0u
			? std::optional<float>{
				static_cast<float>(water_tool_.desired_place_amount()) / static_cast<float>(water_tool_.current_capacity())
			}
			: std::nullopt;

		return {
			.texture = &tools_texture_,
			.icon_rect = tool_icon_rect(
				water_tool_.tier_index(),
				water_tool_.current_amount() > 0u ? 2u : 1u),
			.selected = selected_slot_ == HotbarSlot::Water,
			.show_bar = true,
			.fill_ratio = fill_ratio,
			.overlay_ratio = overlay_ratio,
			.bar_fill = sf::Color(76, 188, 235, 255),
			.bar_frame = sf::Color(63, 144, 206, 240),
			.bar_background = sf::Color(19, 34, 44, 210),
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
			{ static_cast<int>(column) * cell_width, static_cast<int>(row) * cell_height },
			{ cell_width, cell_height }
		};
	}
}
