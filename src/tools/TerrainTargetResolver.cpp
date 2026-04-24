#include "pch.hpp"
#include "tools/TerrainTargetResolver.hpp"

#include "terrain/TerrainCollider.hpp"

namespace game::tools
{
	namespace
	{
		constexpr float max_tool_reach = 5.5f;

		struct TerrainRayCastContext final
		{
			b2BodyId ignored_body{ b2_nullBodyId };
			bool want_water{ false };
			std::optional<vec2> hit_point{ std::nullopt };
		};

		float terrain_ray_cast_callback(const b2ShapeId shape_id, const b2Vec2 point,
			const b2Vec2 /*normal*/, const float fraction, void* context)
		{
			auto& ray_context = *static_cast<TerrainRayCastContext*>(context);
			const auto body_id = b2Shape_GetBody(shape_id);
			if (B2_ID_EQUALS(body_id, ray_context.ignored_body))
			{
				return -1.0f;
			}

			const bool is_water = terrain::is_water_collider_user_data(b2Body_GetUserData(body_id));
			if (is_water != ray_context.want_water)
			{
				return -1.0f;
			}

			ray_context.hit_point = vec2{ point.x, point.y };
			return fraction;
		}

		std::optional<vec2> cast_tool_ray(const TerrainToolContext& context, const bool want_water)
		{
			if (context.terrain == nullptr || !b2World_IsValid(context.world) || !b2Body_IsValid(context.player_body))
			{
				return std::nullopt;
			}

			const vec2 ray_delta{
				context.mouse_world_position.x - context.player_world_position.x,
				context.mouse_world_position.y - context.player_world_position.y
			};
			const float ray_distance = ray_delta.length();
			if (ray_distance <= std::numeric_limits<float>::epsilon()) return std::nullopt;

			const float clamped_distance = std::min(ray_distance, max_tool_reach);
			const float scale = clamped_distance / ray_distance;
			const vec2 translation{
				ray_delta.x * scale,
				ray_delta.y * scale
			};

			TerrainRayCastContext ray_context{
				.ignored_body = context.player_body,
				.want_water = want_water
			};
			const b2QueryFilter filter = b2DefaultQueryFilter();
			b2World_CastRay(
				context.world,
				to_b2(context.player_world_position),
				to_b2(translation),
				filter,
				terrain_ray_cast_callback,
				&ray_context);

			return ray_context.hit_point;
		}
	}

	std::optional<vec2> TerrainTargetResolver::clamped_tool_world_position(const TerrainToolContext& context) const
	{
		if (context.terrain == nullptr || !b2Body_IsValid(context.player_body)) return std::nullopt;

		const vec2 ray_delta{
			context.mouse_world_position.x - context.player_world_position.x,
			context.mouse_world_position.y - context.player_world_position.y
		};
		const float ray_distance = ray_delta.length();
		if (ray_distance <= std::numeric_limits<float>::epsilon()) return std::nullopt;

		const float clamped_distance = std::min(ray_distance, max_tool_reach);
		const float scale = clamped_distance / ray_distance;

		return vec2{
			context.player_world_position.x + ray_delta.x * scale,
			context.player_world_position.y + ray_delta.y * scale
		};
	}

	std::optional<vec2> TerrainTargetResolver::terrain_tool_hit_world_position(const TerrainToolContext& context) const
	{
		return cast_tool_ray(context, false);
	}

	std::optional<vec2> TerrainTargetResolver::water_tool_target_world_position(const TerrainToolContext& context) const
	{
		if (const auto water_hit = cast_tool_ray(context, true); water_hit.has_value()) return water_hit;
		if (const auto hit = terrain_tool_hit_world_position(context); hit.has_value()) return hit;
		return clamped_tool_world_position(context);
	}
}
