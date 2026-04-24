#include "pch.hpp"
#include "tools/TerrainSculptTool.hpp"

#include "Input.hpp"
#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
	void TerrainSculptTool::deactivate()
	{
		reset_brush_state(dig_state_);
		reset_brush_state(place_state_);
	}

	void TerrainSculptTool::update(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
		const float dt)
	{
		if (context.terrain == nullptr) return;

		const auto& tier = current_tier();
		emit_brush_stamps(context, resolver, MouseButton::Left, true, tier.dig, dig_state_, dt);
		emit_brush_stamps(context, resolver, MouseButton::Right, false, tier.place, place_state_, dt);
	}

	void TerrainSculptTool::handle_mouse_pressed(const TerrainToolContext& /*context*/,
		const TerrainTargetResolver& /*resolver*/, const MouseButton /*button*/)
	{
	}

	void TerrainSculptTool::upgrade()
	{
		if (tier_index_ + 1u >= 3u) return;
		++tier_index_;
	}

	std::size_t TerrainSculptTool::tier_index() const
	{
		return tier_index_;
	}

	std::uint32_t TerrainSculptTool::stored_ground() const
	{
		return stored_ground_;
	}

	std::uint32_t TerrainSculptTool::capacity() const
	{
		return current_tier().capacity;
	}

	const TerrainSculptTool::ToolTier& TerrainSculptTool::current_tier() const
	{
		static constexpr std::array<ToolTier, 3> terrain_tool_tiers{{
			ToolTier{
				.dig = { 0.95f, -0.9f, 3.5f, 0.9f, 2.5f },
				.place = { 0.78f, 0.8f, 3.5f, 0.85f, 1.7f },
				.capacity = 2400u
			},
			ToolTier{
				.dig = { 1.28f, -0.95f, 6.0f, 0.72f, 2.2f },
				.place = { 1.0f, 0.85f, 6.0f, 0.68f, 1.55f },
				.capacity = 4800u
			},
			ToolTier{
				.dig = { 1.6f, -1.0f, 9.5f, 0.58f, 2.0f },
				.place = { 1.22f, 0.9f, 9.5f, 0.55f, 1.4f },
				.capacity = 8000u
			}
		}};

		return terrain_tool_tiers[std::min(tier_index_, terrain_tool_tiers.size() - 1u)];
	}

	void TerrainSculptTool::reset_brush_state(BrushState& state)
	{
		state.emission_accumulator = 0.0f;
		state.last_stamp_world.reset();
	}

	void TerrainSculptTool::emit_brush_stamps(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
		const MouseButton button, const bool digging, const BrushConfig& config, BrushState& state, const float dt)
	{
		if (context.terrain == nullptr) return;

		const auto available_units = digging ? capacity() - std::min(stored_ground_, capacity()) : stored_ground_;
		if (!Input::is_pressed(button) || available_units == 0u)
		{
			reset_brush_state(state);
			return;
		}

		const auto world_position = resolver.terrain_tool_hit_world_position(context);
		if (!world_position.has_value())
		{
			reset_brush_state(state);
			return;
		}

		const auto emit_stamp = [&](const vec2 position)
		{
			const auto budget = digging ? capacity() - std::min(stored_ground_, capacity()) : stored_ground_;
			if (budget == 0u) return;

			const auto units = context.terrain->apply_ground_brush(
				terrain::TerrainGenerator::TerrainEdit::make(
					position,
					config.radius,
					config.signed_strength_per_stamp,
					config.falloff_exponent),
				budget);

			if (units == 0u) return;

			if (digging)
			{
				stored_ground_ = std::min(capacity(), stored_ground_ + units);
			}
			else
			{
				stored_ground_ -= std::min(stored_ground_, units);
			}
		};

		const float stamps_per_second = std::max(config.stamps_per_second, 1.0f);
		const float interval = 1.0f / stamps_per_second;
		state.emission_accumulator += dt;

		if (!state.last_stamp_world.has_value())
		{
			emit_stamp(*world_position);
			state.last_stamp_world = *world_position;
			state.emission_accumulator = std::fmod(state.emission_accumulator, interval);
			return;
		}

		const auto previous_position = *state.last_stamp_world;
		const vec2 delta{
			world_position->x - previous_position.x,
			world_position->y - previous_position.y
		};
		const float distance = delta.length();
		const float spacing = std::max(config.radius * config.spacing_factor, 0.05f);
		const int time_stamp_count = static_cast<int>(std::floor(state.emission_accumulator / interval));
		const int movement_stamp_count = static_cast<int>(std::floor(distance / spacing));
		const int stamp_count = std::max(time_stamp_count, movement_stamp_count);

		if (stamp_count <= 0) return;

		if (distance <= std::numeric_limits<float>::epsilon())
		{
			for (int i = 0; i < stamp_count; ++i)
			{
				emit_stamp(*world_position);
			}
		}
		else
		{
			for (int i = 1; i <= stamp_count; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(stamp_count);
				emit_stamp(lerp_vec2(previous_position, *world_position, t));
			}
		}

		state.last_stamp_world = *world_position;
		state.emission_accumulator = std::fmod(state.emission_accumulator, interval);
	}
}
