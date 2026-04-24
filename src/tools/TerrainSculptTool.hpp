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

		void upgrade();

		[[nodiscard]] std::size_t tier_index() const;
		[[nodiscard]] std::uint32_t stored_ground() const;
		[[nodiscard]] std::uint32_t capacity() const;

	private:
		struct BrushConfig final
		{
			float radius{ 1.0f };
			float signed_strength_per_stamp{ 0.0f };
			float stamps_per_second{ 1.0f };
			float spacing_factor{ 1.0f };
			float falloff_exponent{ 1.8f };
		};

		struct ToolTier final
		{
			BrushConfig dig{};
			BrushConfig place{};
			std::uint32_t capacity{ 0u };
		};

		struct BrushState final
		{
			float emission_accumulator{ 0.0f };
			std::optional<vec2> last_stamp_world{ std::nullopt };
		};

		[[nodiscard]] const ToolTier& current_tier() const;
		void reset_brush_state(BrushState& state);
		void emit_brush_stamps(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
			MouseButton button, bool digging, const BrushConfig& config, BrushState& state, float dt);

		std::size_t tier_index_{ 0u };
		std::uint32_t stored_ground_{ 0u };
		BrushState dig_state_{};
		BrushState place_state_{};
	};
}
