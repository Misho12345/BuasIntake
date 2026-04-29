#include "pch.hpp"
#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
	namespace
	{
		constexpr float max_tool_reach = 5.5f;

		struct ToolRayCastContext final
		{
			b2BodyId ignored_body{ b2_nullBodyId };
			std::optional<vec2> hit_point{ std::nullopt };
		};

		float tool_ray_cast_callback(const b2ShapeId shape_id, const b2Vec2 point,
			const b2Vec2 /*normal*/, const float fraction, void* context)
		{
			auto& ray_context = *static_cast<ToolRayCastContext*>(context);
			const auto body_id = b2Shape_GetBody(shape_id);
			if (B2_ID_EQUALS(body_id, ray_context.ignored_body))
			{
				return -1.0f;
			}

			ray_context.hit_point = vec2{ point.x, point.y };
			return fraction;
		}

		std::optional<vec2> tool_ray_translation(const TerrainToolContext& context)
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
			return vec2{
				ray_delta.x * scale,
				ray_delta.y * scale
			};
		}

		std::optional<vec2> cast_tool_ray(const TerrainToolContext& context)
		{
			const auto translation = tool_ray_translation(context);
			if (!translation.has_value()) return std::nullopt;

			ToolRayCastContext ray_context{
				.ignored_body = context.player_body
			};
			const b2QueryFilter filter = b2DefaultQueryFilter();
			b2World_CastRay(
				context.world,
				to_b2(context.player_world_position),
				to_b2(*translation),
				filter,
				tool_ray_cast_callback,
				&ray_context);

			return ray_context.hit_point;
		}
	}

	std::optional<vec2> TerrainTargetResolver::clamped_tool_world_position(const TerrainToolContext& context) const
	{
		const auto translation = tool_ray_translation(context);
		if (!translation.has_value()) return std::nullopt;

		return vec2{
			context.player_world_position.x + translation->x,
			context.player_world_position.y + translation->y
		};
	}

	std::optional<vec2> TerrainTargetResolver::terrain_tool_hit_world_position(const TerrainToolContext& context) const
	{
		return cast_tool_ray(context);
	}

	std::optional<vec2> TerrainTargetResolver::water_tool_target_world_position(const TerrainToolContext& context) const
	{
		if (const auto hit = terrain_tool_hit_world_position(context); hit.has_value()) return hit;
		return clamped_tool_world_position(context);
	}
}
