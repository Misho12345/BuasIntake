#include "pch.hpp"

#include "tools/TerrainTargetResolver.hpp"

#include "terrain/PlanetTerrain.hpp"

namespace game::tools
{
    namespace
    {
        constexpr float max_tool_reach = 7.5f;

        struct ToolRayCastContext final
        {
            b2BodyId            ignored_body{ b2_nullBodyId };
            std::optional<vec2> hit_point{ std::nullopt };
        };

        float tool_ray_cast_callback(
            const b2ShapeId shape_id,
            const b2Vec2    point,
            const b2Vec2 /*normal*/,
            const float fraction,
            void*       context)
        {
            auto& [ignored_body, hit_point] = *static_cast<ToolRayCastContext*>(context);

            const auto body_id = b2Shape_GetBody(shape_id);

            if (B2_ID_EQUALS(body_id, ignored_body)) return -1.0f;
            if (b2Shape_IsSensor(shape_id)) return -1.0f;

            hit_point = from_b2(point);
            return fraction;
        }

        std::optional<vec2> tool_ray_translation(const TerrainToolContext& context)
        {
            if (context.terrain == nullptr ||
                !b2World_IsValid(context.world) ||
                !b2Body_IsValid(context.player_body))
                return std::nullopt;

            const vec2 ray_delta{
                context.mouse_world_position.x - context.player_world_position.x,
                context.mouse_world_position.y - context.player_world_position.y
            };
            const float ray_distance = ray_delta.length();
            if (ray_distance <= eps) return std::nullopt;

            // keep all tool targeting within the same reach limit
            const float clamped_distance = std::min(ray_distance, max_tool_reach);
            const float scale            = clamped_distance / ray_distance;
            return vec2{ ray_delta.x * scale, ray_delta.y * scale };
        }

        std::optional<vec2> cast_tool_ray(const TerrainToolContext& context)
        {
            const auto translation = tool_ray_translation(context);
            if (!translation.has_value()) return std::nullopt;

            ToolRayCastContext  ray_context{ .ignored_body = context.player_body };
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

        std::optional<vec2> find_water_point_along_tool_line(
            const TerrainToolContext&  context,
            const std::optional<vec2>& end_override = std::nullopt)
        {
            if (context.terrain == nullptr) return std::nullopt;

            const auto translation = tool_ray_translation(context);
            if (!translation.has_value()) return std::nullopt;

            const vec2 start = context.player_world_position;
            const vec2 end   = end_override.has_value()
                                   ? *end_override
                                   : vec2{ start.x + translation->x, start.y + translation->y };

            const vec2  effective_translation = end - start;
            const float ray_distance          = effective_translation.length();

            if (ray_distance <= eps) return std::nullopt;

            const float step_length = std::max(
                std::min(context.terrain->terrain_cell_size().x,
                         context.terrain->terrain_cell_size().y) * 0.35f,
                0.05f);

            const int step_count = std::max(2, static_cast<int>(std::ceil(ray_distance / step_length)));

            vec2 previous          = start;
            bool previous_in_water = context.terrain->contains_water_volume(previous);
            for (int step = 1; step <= step_count; ++step)
            {
                const float t                = static_cast<float>(step) / static_cast<float>(step_count);
                const vec2  current          = lerp(start, end, t);
                const bool  current_in_water = context.terrain->contains_water_volume(current);

                if (!current_in_water)
                {
                    previous          = current;
                    previous_in_water = false;
                    continue;
                }

                if (!previous_in_water)
                {
                    vec2 low  = previous;
                    vec2 high = current;

                    // tighten the hit a bit so the bucket target sits near the surface instead of deep inside the blob
                    for (int i = 0; i < 6; ++i)
                    {
                        const vec2 mid = lerp(low, high, 0.5f);
                        if (context.terrain->contains_water_volume(mid)) high = mid;
                        else low = mid;
                    }
                    return high;
                }

                return current;
            }

            return std::nullopt;
        }
    }

    std::optional<vec2> TerrainTargetResolver::tool_reach_world_position(const TerrainToolContext& context) const
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

    std::optional<vec2> TerrainTargetResolver::water_pickup_target_world_position(
        const TerrainToolContext& context) const
    {
        if (const auto water_hit = find_water_point_along_tool_line(context);
            water_hit.has_value())
            return water_hit;

        const auto clamped_position = tool_reach_world_position(context);
        if (context.terrain != nullptr &&
            clamped_position.has_value() &&
            context.terrain-> contains_water_volume(*clamped_position))
            return clamped_position;

        const auto hit = terrain_tool_hit_world_position(context);
        if (context.terrain != nullptr && hit.has_value() &&
            context.terrain->contains_water_volume(*hit))
        {
            return hit;
        }

        if (hit.has_value()) return hit;
        return clamped_position;
    }

    std::optional<vec2> TerrainTargetResolver::water_placement_target_world_position(
        const TerrainToolContext& context) const
    {
        const auto terrain_hit = terrain_tool_hit_world_position(context);

        if (const auto water_hit = find_water_point_along_tool_line(context, terrain_hit);
            water_hit.has_value())
            return water_hit;

        if (terrain_hit.has_value()) return terrain_hit;

        return tool_reach_world_position(context);
    }
}
