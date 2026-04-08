#pragma once

#include "pch.hpp"

#include "tools/ToolStrategy.hpp"

namespace game::tools
{
	class WaterTool final : public IToolStrategy
	{
	public:
		WaterTool() = default;
		~WaterTool() override = default;

		void update(const TerrainToolContext& context, const TerrainTargetResolver& resolver, float dt) override;
		void handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
			MouseButton button) override;

	private:
		std::uint32_t water_volume_cap_{ 5000u };
	};
}
