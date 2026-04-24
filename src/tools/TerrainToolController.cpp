#include "pch.hpp"
#include "tools/TerrainToolController.hpp"

namespace game::tools
{
	namespace
	{
		void draw_panel(sf::RenderTarget& target, const sf::Vector2f position, const sf::Vector2f size,
			const sf::Color fill, const sf::Color outline, const float outline_thickness = 1.5f)
		{
			sf::RectangleShape shape{ size };
			shape.setPosition(position);
			shape.setFillColor(fill);
			shape.setOutlineColor(outline);
			shape.setOutlineThickness(outline_thickness);
			target.draw(shape);
		}

		void draw_vertical_bar(sf::RenderTarget& target, const sf::Vector2f position, const sf::Vector2f size,
			const float ratio, const sf::Color fill_color, const sf::Color frame_color,
			const sf::Color background_color, const std::optional<float> overlay_ratio = std::nullopt,
			const sf::Color overlay_color = sf::Color::Transparent)
		{
			sf::RectangleShape frame{ size };
			frame.setPosition(position);
			frame.setFillColor(background_color);
			frame.setOutlineColor(frame_color);
			frame.setOutlineThickness(1.5f);
			target.draw(frame);

			const sf::Vector2f inner_size{
				std::max(size.x - 4.0f, 0.0f),
				std::max(size.y - 4.0f, 0.0f)
			};
			const sf::Vector2f inner_position{ position.x + 2.0f, position.y + 2.0f };

			sf::RectangleShape gutter{ inner_size };
			gutter.setPosition(inner_position);
			gutter.setFillColor(sf::Color(15, 20, 28, 210));
			target.draw(gutter);

			const float clamped_ratio = std::clamp(ratio, 0.0f, 1.0f);
			if (clamped_ratio > 0.0f)
			{
				const float fill_height = inner_size.y * clamped_ratio;
				sf::RectangleShape fill{ { inner_size.x, fill_height } };
				fill.setPosition({ inner_position.x, inner_position.y + inner_size.y - fill_height });
				fill.setFillColor(fill_color);
				target.draw(fill);
			}

			if (!overlay_ratio.has_value()) return;

			const float clamped_overlay = std::clamp(*overlay_ratio, 0.0f, clamped_ratio);
			if (clamped_overlay <= 0.0f) return;

			const float overlay_height = inner_size.y * clamped_overlay;
			sf::RectangleShape overlay{
				{ std::max(inner_size.x - 4.0f, 0.0f), overlay_height }
			};
			overlay.setPosition({ inner_position.x + 2.0f, inner_position.y + inner_size.y - overlay_height });
			overlay.setFillColor(overlay_color);
			target.draw(overlay);
		}
	}

	void TerrainToolController::initialize_ui_assets()
	{
		if (ui_assets_ready_) return;

		if (!tools_texture_.loadFromFile("assets/images/tools.png"))
		{
			throw std::runtime_error("Failed to load tools sprite sheet");
		}
		tools_texture_.setSmooth(false);

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

		selected_slot_ = is_water_slot_selected() ? HotbarSlot::Digging : HotbarSlot::Water;
		sync_active_tool();
	}

	void TerrainToolController::handle_upgrade()
	{
		if (is_water_slot_selected()) water_tool_.upgrade();
		else terrain_tool_.upgrade();
	}

	void TerrainToolController::refill_bucket()
	{
		water_tool_.fill_to_capacity();
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

		const auto target_size = target.getSize();
		const float width = static_cast<float>(target_size.x);
		const float height = static_cast<float>(target_size.y);
		const float slot_size = 74.0f;
		const float slot_gap = 12.0f;
		const float panel_padding_x = 14.0f;
		const float panel_padding_y = 14.0f;
		const float bar_width = 22.0f;
		const float bar_height = 108.0f;
		const float bar_gap = 10.0f;
		const float hotbar_width = panel_padding_x * 2.0f + slot_size * 2.0f + slot_gap;
		const float hotbar_height = panel_padding_y * 2.0f + slot_size + bar_gap + bar_height;
		const sf::Vector2f hotbar_position{
			width - hotbar_width - 22.0f,
			height - hotbar_height - 22.0f
		};

		draw_panel(
			target,
			hotbar_position,
			{ hotbar_width, hotbar_height },
			sf::Color(13, 20, 28, 228),
			sf::Color(63, 82, 97, 236),
			2.0f);

		const std::array<bool, 2> selected_slots{
			selected_slot_ == HotbarSlot::Digging,
			selected_slot_ == HotbarSlot::Water
		};
		const std::array icon_rows{
			std::size_t{ 0u },
			water_tool_.current_amount() > 0u ? std::size_t{ 2u } : std::size_t{ 1u }
		};
		const std::array icon_columns{
			terrain_tool_.tier_index(),
			water_tool_.tier_index()
		};
		const float ground_ratio = terrain_tool_.capacity() == 0u ? 0.0f :
			static_cast<float>(terrain_tool_.stored_ground()) / static_cast<float>(terrain_tool_.capacity());
		const float water_ratio = water_tool_.current_capacity() == 0u ? 0.0f :
			static_cast<float>(water_tool_.current_amount()) / static_cast<float>(water_tool_.current_capacity());
		const std::optional<float> desired_water_ratio = water_tool_.is_placement_mode() && water_tool_.current_capacity() > 0u ?
			std::optional<float>{ static_cast<float>(water_tool_.desired_place_amount()) / static_cast<float>(water_tool_.current_capacity()) } :
			std::nullopt;

		for (std::size_t i = 0; i < 2u; ++i)
		{
			const sf::Vector2f slot_position{
				hotbar_position.x + panel_padding_x + static_cast<float>(i) * (slot_size + slot_gap),
				hotbar_position.y + panel_padding_y + bar_height + bar_gap
			};
			const sf::Vector2f bar_position{
				slot_position.x + (slot_size - bar_width) * 0.5f,
				hotbar_position.y + panel_padding_y
			};
			const sf::Color frame = selected_slots[i] ? sf::Color(247, 221, 142, 255) : sf::Color(72, 90, 103, 230);
			const sf::Color fill = selected_slots[i] ? sf::Color(50, 63, 73, 245) : sf::Color(26, 36, 47, 235);
			const float ratio = i == 0u ? ground_ratio : water_ratio;
			const sf::Color bar_fill = i == 0u ? sf::Color(224, 161, 74, 255) : sf::Color(76, 188, 235, 255);
			const sf::Color bar_frame = i == 0u ? sf::Color(185, 127, 60, 240) : sf::Color(63, 144, 206, 240);
			const sf::Color bar_back = i == 0u ? sf::Color(45, 31, 22, 210) : sf::Color(19, 34, 44, 210);

			draw_vertical_bar(
				target,
				bar_position,
				{ bar_width, bar_height },
				ratio,
				bar_fill,
				bar_frame,
				bar_back,
				i == 1u ? desired_water_ratio : std::nullopt,
				sf::Color(191, 245, 255, 210));

			draw_panel(target, slot_position, { slot_size, slot_size }, fill, frame, 2.0f);

			sf::Sprite icon{ tools_texture_, tool_icon_rect(icon_columns[i], icon_rows[i]) };
			const auto bounds = icon.getLocalBounds();
			icon.setOrigin(bounds.getCenter());
			const float scale = std::min(46.0f / bounds.size.x, 46.0f / bounds.size.y);
			icon.setScale({ scale, scale });
			icon.setPosition({ slot_position.x + slot_size * 0.5f, slot_position.y + slot_size * 0.5f });
			target.draw(icon);

			if (i == 1u && water_tool_.is_placement_mode())
			{
				sf::RectangleShape aim_ring{ { slot_size - 12.0f, slot_size - 12.0f } };
				aim_ring.setPosition({ slot_position.x + 6.0f, slot_position.y + 6.0f });
				aim_ring.setFillColor(sf::Color::Transparent);
				aim_ring.setOutlineThickness(1.5f);
				aim_ring.setOutlineColor(sf::Color(194, 244, 255, 220));
				target.draw(aim_ring);
			}
		}
	}

	void TerrainToolController::export_current_chunk_field(const TerrainToolContext& context) const
	{
		if (context.terrain == nullptr) return;

		const auto output_path = context.terrain->save_chunk_field_image(context.player_world_position);
		if (output_path.has_value())
		{
			std::println("Saved chunk field image to {}", output_path->string());
		}
		else
		{
			std::println(std::cerr, "Failed to save chunk field image");
		}
	}

	void TerrainToolController::sync_active_tool()
	{
		IToolStrategy* next_tool = is_water_slot_selected() ? static_cast<IToolStrategy*>(&water_tool_)
			: static_cast<IToolStrategy*>(&terrain_tool_);
		if (next_tool == active_tool_) return;

		active_tool_->deactivate();
		active_tool_ = next_tool;
		active_tool_->activate();
	}

	bool TerrainToolController::is_water_slot_selected() const
	{
		return selected_slot_ == HotbarSlot::Water;
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
