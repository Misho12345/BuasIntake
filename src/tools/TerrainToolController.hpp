#pragma once

#include "pch.hpp"

#include "tools/SeedTool.hpp"
#include "tools/TerrainToolHudRenderer.hpp"
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

		[[nodiscard]] Result<void> initialize_ui_assets();
		void update(const TerrainToolContext& context, float dt);
		void handle_mouse_pressed(const TerrainToolContext& context, MouseButton button);
		void handle_scroll(float delta);
		void handle_upgrade();
		void handle_zero_shortcut(const TerrainToolContext& context);
		void cancel_bucket_placement();
		void draw_world_preview(const TerrainToolContext& context, const sf::View& view) const;
		void draw_ui(sf::RenderTarget& target) const;
		void export_current_chunk_field(const TerrainToolContext& context) const;

	private:
		static constexpr std::size_t hotbar_slot_count{ 3u };

		enum class HotbarSlot : std::uint8_t
		{
			Digging = 0,
			Water = 1,
			Seeds = 2
		};

		void sync_active_tool();
		[[nodiscard]] std::array<TerrainToolHudSlotData, hotbar_slot_count> build_hud_slots() const;
		[[nodiscard]] TerrainToolHudSlotData build_digging_slot_data() const;
		[[nodiscard]] TerrainToolHudSlotData build_water_slot_data() const;
		[[nodiscard]] TerrainToolHudSlotData build_seed_slot_data() const;
		[[nodiscard]] bool is_water_slot_selected() const;
		[[nodiscard]] bool is_seed_slot_selected() const;
		[[nodiscard]] sf::IntRect tool_icon_rect(std::size_t column, std::size_t row) const;

		TerrainTargetResolver target_resolver_{};
		TerrainSculptTool terrain_tool_{};
		WaterTool water_tool_{};
		SeedTool seed_tool_{};
		IToolStrategy* active_tool_{ &terrain_tool_ };
		HotbarSlot selected_slot_{ HotbarSlot::Digging };
		sf::Texture tools_texture_{};
		sf::Texture seed_icon_texture_{};
		bool ui_assets_ready_{ false };
	};
}
