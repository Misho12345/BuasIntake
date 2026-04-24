#pragma once

#include "pch.hpp"

#include "tools/TerrainSculptTool.hpp"
#include "tools/TerrainTargetResolver.hpp"
#include "tools/WaterTool.hpp"

namespace game::tools
{
	class TerrainToolController final
	{
	public:
		TerrainToolController() = default;
		~TerrainToolController() = default;

		TerrainToolController(const TerrainToolController&) = delete;
		TerrainToolController& operator=(const TerrainToolController&) = delete;
		TerrainToolController(TerrainToolController&&) noexcept = default;
		TerrainToolController& operator=(TerrainToolController&&) noexcept = default;

		void initialize_ui_assets();
		void update(const TerrainToolContext& context, float dt);
		void handle_mouse_pressed(const TerrainToolContext& context, MouseButton button);
		void handle_scroll(float delta);
		void handle_upgrade();
		void refill_bucket();
		void cancel_bucket_placement();
		void draw_world_preview(const TerrainToolContext& context, const sf::View& view) const;
		void draw_ui(sf::RenderTarget& target) const;
		void export_current_chunk_field(const TerrainToolContext& context) const;

	private:
		enum class HotbarSlot : std::uint8_t
		{
			Digging = 0,
			Water = 1
		};

		void sync_active_tool();
		[[nodiscard]] bool is_water_slot_selected() const;
		[[nodiscard]] sf::IntRect tool_icon_rect(std::size_t column, std::size_t row) const;

		TerrainTargetResolver target_resolver_{};
		TerrainSculptTool terrain_tool_{};
		WaterTool water_tool_{};
		IToolStrategy* active_tool_{ &terrain_tool_ };
		HotbarSlot selected_slot_{ HotbarSlot::Digging };
		sf::Texture tools_texture_{};
		bool ui_assets_ready_{ false };
	};
}
