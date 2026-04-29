#include "pch.hpp"
#include "tools/SeedTool.hpp"

#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
	void SeedTool::update(const TerrainToolContext& /*context*/, const TerrainTargetResolver& /*resolver*/, const float /*dt*/)
	{
	}

	void SeedTool::handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
		const MouseButton button)
	{
		if (button != MouseButton::Left || context.terrain == nullptr) return;

		const auto world_position = resolver.terrain_tool_hit_world_position(context);
		if (!world_position.has_value()) return;

		static_cast<void>(context.terrain->plant_seed(*world_position));
	}
}
