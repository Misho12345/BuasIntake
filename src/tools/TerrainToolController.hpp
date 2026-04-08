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

		void update(const TerrainToolContext& context, float dt);
		void handle_mouse_pressed(const TerrainToolContext& context, MouseButton button);
		void export_current_chunk_field(const TerrainToolContext& context) const;

	private:
		void sync_active_tool();
		[[nodiscard]] bool is_water_modifier_active() const;

		TerrainTargetResolver target_resolver_{};
		TerrainSculptTool terrain_tool_{};
		WaterTool water_tool_{};
		IToolStrategy* active_tool_{ &terrain_tool_ };
	};
}
