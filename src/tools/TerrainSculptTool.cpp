#include "pch.hpp"
#include "tools/TerrainSculptTool.hpp"

#include "Input.hpp"
#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
		namespace
		{
			constexpr vec2 placement_blocker_padding{ 1.15f, 0.55f };

			std::optional<terrain::GroundBrushBlocker> make_player_ground_brush_blocker(
				const TerrainToolContext& context)
			{
			if (context.terrain == nullptr || !b2Body_IsValid(context.player_body)) return std::nullopt;

			const b2AABB player_bounds = b2Body_ComputeAABB(context.player_body);
			const vec2 up = normalize_vec2(
				subtract_vec2(context.player_world_position, context.terrain->planet_center()),
				{ 0.0f, 1.0f });
			const vec2 right{ up.y, -up.x };
			const vec2 extents = {
				(player_bounds.upperBound.x - player_bounds.lowerBound.x) * 0.5f,
				(player_bounds.upperBound.y - player_bounds.lowerBound.y) * 0.5f
			};

			return terrain::GroundBrushBlocker{
				.center = context.player_world_position,
				.right = right,
				.up = up,
				.half_extents = {
					std::abs(right.x) * extents.x + std::abs(right.y) * extents.y + placement_blocker_padding.x,
					std::abs(up.x) * extents.x + std::abs(up.y) * extents.y + placement_blocker_padding.y
				}
			};
		}
		}

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

	void TerrainSculptTool::handle_mouse_pressed(const TerrainToolContext& context,
		const TerrainTargetResolver& resolver, const MouseButton button)
	{
		if (button != MouseButton::Left || context.terrain == nullptr) return;

		const auto world_position = resolver.terrain_tool_hit_world_position(context);
		if (!world_position.has_value()) return;

		static_cast<void>(context.terrain->try_harvest_resource(*world_position));
	}

	void TerrainSculptTool::upgrade()
	{
		if (tier_index_ + 1u >= 3u) return;
		++tier_index_;
	}

	void TerrainSculptTool::clear_storage()
	{
		stored_ground_ = 0u;
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
				.dig = { .radius         = 1.5f, .signed_strength_per_stamp = -1.45f, .stamps_per_second = 22.0f,
				         .spacing_factor = 0.6f, .falloff_exponent          = 1.85f },
				.place = { .radius         = 1.5f, .signed_strength_per_stamp = 1.45f, .stamps_per_second = 22.0f,
				           .spacing_factor = 0.6f, .falloff_exponent          = 1.85f },
				.capacity = 6000u
			},
			ToolTier{
				.dig = { .radius         = 1.92f, .signed_strength_per_stamp = -1.75f, .stamps_per_second = 38.0f,
				         .spacing_factor = 0.42f, .falloff_exponent          = 1.65f },
				.place = { .radius         = 1.92f, .signed_strength_per_stamp = 1.75f, .stamps_per_second = 38.0f,
				           .spacing_factor = 0.42f, .falloff_exponent          = 1.65f },
				.capacity = 12000u
			},
			ToolTier{
				.dig = { .radius         = 2.35f, .signed_strength_per_stamp = -2.15f, .stamps_per_second = 68.0f,
				         .spacing_factor = 0.34f, .falloff_exponent          = 1.48f },
				.place = { .radius         = 2.35f, .signed_strength_per_stamp = 2.15f, .stamps_per_second = 68.0f,
				           .spacing_factor = 0.34f, .falloff_exponent          = 1.48f },
				.capacity = 22000u
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

			const auto blocker = digging ? std::nullopt : make_player_ground_brush_blocker(context);
			const auto edit = terrain::TerrainGenerator::TerrainEdit::make(
				position,
				config.radius,
				config.signed_strength_per_stamp,
				config.falloff_exponent);

			const auto units = context.terrain->apply_ground_brush(
				edit,
				budget,
				blocker);

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
