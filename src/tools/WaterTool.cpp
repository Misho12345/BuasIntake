#include "pch.hpp"
#include "tools/WaterTool.hpp"

#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
	void WaterTool::update(const TerrainToolContext& /*context*/, const TerrainTargetResolver& /*resolver*/, const float /*dt*/)
	{
	}

	void WaterTool::handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
		const MouseButton button)
	{
		if (context.terrain == nullptr) return;

		const auto world_position = resolver.water_tool_target_world_position(context);
		if (!world_position.has_value()) return;

		if (button == MouseButton::Right)
		{
			context.terrain->place_water(*world_position, water_volume_cap_);
		}
		else if (button == MouseButton::Left)
		{
			context.terrain->pickup_water(*world_position, water_volume_cap_);
		}
	}
}
