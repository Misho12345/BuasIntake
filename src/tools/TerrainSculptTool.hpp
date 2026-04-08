#pragma once

#include "pch.hpp"

#include "tools/ToolStrategy.hpp"

namespace game::tools
{
	class TerrainSculptTool final : public IToolStrategy
	{
	public:
		TerrainSculptTool() = default;
		~TerrainSculptTool() override = default;

		void deactivate() override;
		void update(const TerrainToolContext& context, const TerrainTargetResolver& resolver, float dt) override;
		void handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
			MouseButton button) override;

	private:
		struct BrushConfig final
		{
			float radius{ 1.2f };
			float signed_strength_per_stamp{ 0.0f };
			float stamps_per_second{ 30.0f };
			float spacing_factor{ 0.5f };
			float falloff_exponent{ 1.8f };
		};

		struct BrushState final
		{
			float emission_accumulator{ 0.0f };
			std::optional<vec2> last_stamp_world{ std::nullopt };
		};

		void reset_brush_state(BrushState& state);
		void emit_brush_stamps(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
			MouseButton button, const BrushConfig& config, BrushState& state, float dt);

		BrushConfig dig_brush_{ 2.25f, -0.85f, 60.0f, 2.45f, 2.8f };
		BrushConfig place_brush_{ 1.05f, 0.8f, 60.0f, 1.45f, 1.5f };
		BrushState dig_state_{};
		BrushState place_state_{};
	};
}
