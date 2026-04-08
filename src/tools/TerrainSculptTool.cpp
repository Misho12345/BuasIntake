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

		emit_brush_stamps(context, resolver, MouseButton::Left, dig_brush_, dig_state_, dt);
		emit_brush_stamps(context, resolver, MouseButton::Right, place_brush_, place_state_, dt);
		context.terrain->apply_pending_edits();
	}

	void TerrainSculptTool::handle_mouse_pressed(const TerrainToolContext& /*context*/,
		const TerrainTargetResolver& /*resolver*/, const MouseButton /*button*/)
	{
	}

	void TerrainSculptTool::reset_brush_state(BrushState& state)
	{
		state.emission_accumulator = 0.0f;
		state.last_stamp_world.reset();
	}

	void TerrainSculptTool::emit_brush_stamps(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
		const MouseButton button, const BrushConfig& config, BrushState& state, const float dt)
	{
		if (context.terrain == nullptr) return;

		if (!Input::is_pressed(button))
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
			context.terrain->queue_edit(terrain::TerrainGenerator::TerrainEdit::make(
				position,
				config.radius,
				config.signed_strength_per_stamp,
				config.falloff_exponent));
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
