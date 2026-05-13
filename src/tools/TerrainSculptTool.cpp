#include "pch.hpp"

#include "tools/TerrainSculptTool.hpp"

#include "tools/ToolProgression.hpp"

#include "platform/InputSystem.hpp"
#include "terrain/PlanetTerrain.hpp"
#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
    namespace
    {
        constexpr float placement_blocker_padding_radius  = 0.10f;
        constexpr float placement_lift_padding            = 0.10f;
        constexpr float placement_lift_horizontal_padding = 0.18f;
        constexpr float placement_lift_cooldown_seconds   = 0.08f;
        constexpr float placement_ceiling_clearance       = 0.06f;

        struct PlacementLiftRayContext final
        {
            b2BodyId ignored_body{ b2_nullBodyId };
            bool     hit{ false };
            float    fraction{ 1.0f };
        };

        struct PlayerPlacementFrame final
        {
            vec2 up{ 0.0f, 1.0f };
            vec2 right{ 1.0f, 0.0f };
            vec2 extents{ 0.0f, 0.0f };
        };

        float placement_lift_ray_cast_callback(
            const b2ShapeId shape_id,
            const b2Vec2 /*point*/,
            const b2Vec2 /*normal*/,
            const float fraction,
            void*       context)
        {
            auto& ray_context = *static_cast<PlacementLiftRayContext*>(context);
            if (B2_ID_EQUALS(b2Shape_GetBody(shape_id), ray_context.ignored_body) ||
                b2Shape_IsSensor(shape_id))
                return -1.0f;

            ray_context.hit      = true;
            ray_context.fraction = fraction;
            return fraction;
        }

        std::optional<PlayerPlacementFrame> player_placement_frame(const TerrainToolContext& context)
        {
            if (context.terrain == nullptr || !b2Body_IsValid(context.player_body)) return std::nullopt;

            const b2AABB player_bounds = b2Body_ComputeAABB(context.player_body);
            const vec2 up = normalize(
                context.player_world_position - context.terrain->planet_center(),
                { 0.0f, 1.0f });

            return PlayerPlacementFrame{
                .up = up,
                .right = { up.y, -up.x },
                .extents = {
                    (player_bounds.upperBound.x - player_bounds.lowerBound.x) * 0.5f,
                    (player_bounds.upperBound.y - player_bounds.lowerBound.y) * 0.5f
                }
            };
        }

        std::optional<terrain::GroundBrushBlocker> make_player_ground_brush_blocker(const TerrainToolContext& context)
        {
            const auto frame = player_placement_frame(context);
            if (!frame.has_value()) return std::nullopt;

            const float player_radius = std::max(frame->extents.y, frame->extents.x + placement_blocker_padding_radius);

            // placing ground right into the player feels bad, so placement keeps a little safety gap around them
            return terrain::GroundBrushBlocker{
                .center       = context.player_world_position,
                .right        = frame->right,
                .up           = frame->up,
                .half_extents = { 0.0f, 0.0f },
                .radius       = player_radius
            };
        }

        // when placement happens too close under the player the obvious target is actually the wrong one
        // nudging the stamp down toward the feet line keeps ground placement from fighting the player body every frame
        vec2 adjusted_ground_placement_position(
            const TerrainToolContext& context,
            const vec2                placement_position,
            const float               brush_radius)
        {
            const auto frame = player_placement_frame(context);
            if (!frame.has_value()) return placement_position;

            const vec2  delta          = placement_position - context.player_world_position;
            const float lateral_offset = std::abs(delta.dot(frame->right));
            const float up_offset      = delta.dot(frame->up);

            if (lateral_offset > frame->extents.x + placement_lift_horizontal_padding) return placement_position;
            if (up_offset > 0.05f) return placement_position;

            const vec2 feet_position = context.player_world_position - frame->up * frame->extents.y;
            return feet_position - frame->up * brush_radius;
        }

        // this is the boring safety net that keeps the player from getting sealed into fresh terrain
        // we only lift a little we cap it we raycast for ceilings and we add a cooldown because otherwise this turns into a pogo stick
        void lift_player_for_ground_placement(
            const TerrainToolContext& context,
            const vec2                placement_position,
            const float               brush_radius,
            float&                    lift_cooldown)
        {
            const auto frame = player_placement_frame(context);
            if (!frame.has_value()) return;
            if (lift_cooldown > 0.0f) return;

            const vec2  delta            = placement_position - context.player_world_position;
            const float lateral_offset   = std::abs(delta.dot(frame->right));
            const float up_offset        = delta.dot(frame->up);
            const float horizontal_limit = frame->extents.x + placement_lift_horizontal_padding;

            if (lateral_offset > horizontal_limit) return;
            if (up_offset > 0.05f) return;
            if (up_offset < -(frame->extents.y + placement_lift_padding) * 1.5f) return;

            const float base_clearance = frame->extents.y + placement_lift_padding + up_offset;
            const float radius_lift    = std::max(brush_radius * 0.30f, 0.0f);
            const float max_lift       = std::max(frame->extents.y * 0.62f, brush_radius * 0.28f);
            const float lift_distance  = std::clamp(std::max(base_clearance, radius_lift), 0.0f, max_lift);
            if (lift_distance <= 1e-4f) return;

            const vec2 current_position = from_b2(b2Body_GetPosition(context.player_body));
            const vec2 top_origin       = current_position + frame->up * frame->extents.y;

            PlacementLiftRayContext ray_context{ .ignored_body = context.player_body };
            const float             ray_distance = lift_distance + placement_ceiling_clearance;

            const b2QueryFilter filter = b2DefaultQueryFilter();

            b2World_CastRay(
                context.world,
                to_b2(top_origin),
                to_b2(frame->up * ray_distance),
                filter,
                placement_lift_ray_cast_callback,
                &ray_context);

            float capped_lift_distance = lift_distance;
            if (ray_context.hit)
            {
                capped_lift_distance = std::max(
                    ray_distance * ray_context.fraction - placement_ceiling_clearance,
                    0.0f);
            }

            if (capped_lift_distance <= 1e-4f) return;

            const vec2 next_position    = current_position + frame->up * capped_lift_distance;
            const auto current_rotation = b2Body_GetRotation(context.player_body);

            b2Body_SetLinearVelocity(context.player_body, { .x = 0.0f, .y = 0.0f });
            b2Body_SetTransform(context.player_body, to_b2(next_position), current_rotation);
            lift_cooldown = placement_lift_cooldown_seconds;
        }
    }

    void TerrainSculptTool::BrushStroke::begin()
    {
        active = true;
    }

    void TerrainSculptTool::BrushStroke::reset()
    {
        active               = false;
        emission_accumulator = 0.0f;
        last_stamp_world.reset();
    }

    void TerrainSculptTool::deactivate()
    {
        dig_state_.reset();
        place_state_.reset();
        suppress_left_stroke_until_released_ = false;
    }

    // held mouse buttons emit a stream of brush stamps instead of one edit per click
    void TerrainSculptTool::update(
        const TerrainToolContext&    context,
        const TerrainTargetResolver& resolver,
        const float                  dt)
    {
        if (context.terrain == nullptr) return;

        placement_lift_cooldown_ = std::max(placement_lift_cooldown_ - dt, 0.0f);

        const auto& tier = current_tier();
        emit_brush_stamps(context, resolver, MouseButton::Left, true, tier.dig, dig_state_, dt);
        emit_brush_stamps(context, resolver, MouseButton::Right, false, tier.place, place_state_, dt);
    }

    void TerrainSculptTool::handle_mouse_pressed(
        const TerrainToolContext&    context,
        const TerrainTargetResolver& resolver,
        const MouseButton            button)
    {
        if (button != MouseButton::Left || context.terrain == nullptr) return;

        const auto world_position = resolver.terrain_tool_hit_world_position(context);
        if (!world_position.has_value()) return;

        if (context.terrain->try_harvest_resource(*world_position))
        {
            // a successful harvest consumes this press so the same click does not also start digging
            suppress_left_stroke_until_released_ = true;
            dig_state_.reset();
        }
    }

    void TerrainSculptTool::upgrade()
    {
        if (at_max_upgrade()) return;
        auto [next_material, next_level] = next_tool_material_level(material_index_, level_index_);
        material_index_ = next_material;
        level_index_    = next_level;
    }

    std::size_t TerrainSculptTool::material_index() const { return material_index_; }
    std::size_t TerrainSculptTool::level_index() const { return level_index_; }

    bool TerrainSculptTool::at_max_upgrade() const { return at_max_tool_upgrade(material_index_, level_index_); }

    TerrainSculptTool::ToolStats TerrainSculptTool::current_stats() const
    {
        const auto& tier = current_tier();
        return {
            .radius   = tier.dig.radius,
            .speed    = tier.dig.stamps_per_second,
            .capacity = tier.capacity
        };
    }

    std::optional<TerrainSculptTool::ToolStats> TerrainSculptTool::next_stats() const
    {
        if (at_max_upgrade()) return std::nullopt;

        auto [next_material, next_level] = next_tool_material_level(material_index_, level_index_);
        const auto& tier = terrain_tool_tier(flat_tool_tier_index(next_material, next_level));
        return ToolStats{
            .radius   = tier.dig.radius,
            .speed    = tier.dig.stamps_per_second,
            .capacity = tier.capacity
        };
    }

    std::uint32_t TerrainSculptTool::stored_ground() const { return stored_ground_; }
    std::uint32_t TerrainSculptTool::capacity() const { return current_tier().capacity; }

    const TerrainSculptTool::ToolTier& TerrainSculptTool::current_tier() const
    {
        return terrain_tool_tier(flat_tier_index());
    }

    const TerrainSculptTool::ToolTier& TerrainSculptTool::terrain_tool_tier(const std::size_t tier_index)
    {
        static constexpr auto make_tier = [](
            const float         radius,
            const float         strength,
            const float         stamps_per_second,
            const float         spacing_factor,
            const float         falloff_exponent,
            const std::uint32_t capacity)
        {
            const BrushConfig dig{
                .radius                    = radius,
                .signed_strength_per_stamp = -strength,
                .stamps_per_second         = stamps_per_second,
                .spacing_factor            = spacing_factor,
                .falloff_exponent          = falloff_exponent
            };

            return ToolTier{
                .dig = dig,
                .place = {
                    .radius                    = radius,
                    .signed_strength_per_stamp = strength,
                    .stamps_per_second         = stamps_per_second,
                    .spacing_factor            = spacing_factor,
                    .falloff_exponent          = falloff_exponent
                },
                .capacity = capacity
            };
        };

        static constexpr std::array<ToolTier, 9> terrain_tool_tiers{
            {
                make_tier(1.85f, 1.15f, 34.0f, 0.60f, 1.85f, 6000u),
                make_tier(1.98f, 1.24f, 40.0f, 0.42f, 1.65f, 8000u),
                make_tier(2.10f, 1.34f, 46.0f, 0.34f, 1.48f, 10000u),
                make_tier(2.24f, 1.47f, 54.0f, 0.46f, 1.62f, 13000u),
                make_tier(2.38f, 1.60f, 62.0f, 0.42f, 1.58f, 16000u),
                make_tier(2.52f, 1.74f, 70.0f, 0.38f, 1.54f, 19000u),
                make_tier(2.66f, 1.86f, 76.0f, 0.35f, 1.51f, 22000u),
                make_tier(2.78f, 1.98f, 84.0f, 0.34f, 1.49f, 25000u),
                make_tier(2.90f, 2.10f, 92.0f, 0.33f, 1.47f, 29000u)
            }
        };

        static_assert(terrain_tool_tiers.size() == (max_tool_material_index + 1u) * tool_levels_per_material);
        return terrain_tool_tiers[std::min(tier_index, terrain_tool_tiers.size() - 1u)];
    }

    std::size_t TerrainSculptTool::flat_tier_index() const
    {
        return flat_tool_tier_index(material_index_, level_index_);
    }

    void TerrainSculptTool::emit_brush_stamps(
        const TerrainToolContext&    context,
        const TerrainTargetResolver& resolver,
        const MouseButton            button,
        const bool                   digging,
        const BrushConfig&           config,
        BrushStroke&                 state,
        const float                  dt)
    {
        if (context.terrain == nullptr) return;

        if (digging && suppress_left_stroke_until_released_)
        {
            if (context.input != nullptr && context.input->is_pressed(MouseButton::Left))
            {
                state.reset();
                return;
            }

            suppress_left_stroke_until_released_ = false;
        }

        const auto available_units = digging ? capacity() - std::min(stored_ground_, capacity()) : stored_ground_;
        if (context.input == nullptr || !context.input->is_pressed(button) || available_units == 0u)
        {
            state.reset();
            return;
        }

        if (!state.active) state.begin();

        const auto world_position = resolver.terrain_tool_hit_world_position(context);
        if (!world_position.has_value())
        {
            state.reset();
            return;
        }

        const auto emit_stamp = [&](const vec2 position)
        {
            const auto budget = digging ? capacity() - std::min(stored_ground_, capacity()) : stored_ground_;
            if (budget == 0u) return;

            const auto blocker = digging
                                     ? std::nullopt
                                     : make_player_ground_brush_blocker(context);

            const vec2 effective_position = digging
                                                ? position
                                                : adjusted_ground_placement_position(context, position, config.radius);

            const auto edit = terrain::TerrainGenerator::TerrainEdit::make(
                effective_position, config.radius,
                config.signed_strength_per_stamp,
                config.falloff_exponent);

            const auto units = context.terrain->apply_ground_brush(edit, budget, blocker);

            if (units == 0u) return;

            if (digging) stored_ground_ = std::min(capacity(), stored_ground_ + units);
            else
            {
                stored_ground_ -= std::min(stored_ground_, units);
                lift_player_for_ground_placement(context, effective_position, config.radius, placement_lift_cooldown_);
            }
        };

        const float stamps_per_second = std::max(config.stamps_per_second, 1.0f);
        const float interval          = 1.0f / stamps_per_second;
        state.emission_accumulator    += dt;

        if (!state.last_stamp_world.has_value())
        {
            emit_stamp(*world_position);
            state.last_stamp_world     = *world_position;
            state.emission_accumulator = std::fmod(state.emission_accumulator, interval);
            return;
        }

        const auto previous_position = *state.last_stamp_world;

        const vec2 delta{
            world_position->x - previous_position.x,
            world_position->y - previous_position.y
        };

        const float distance = delta.length();
        const float spacing  = std::max(config.radius * config.spacing_factor, 0.05f);

        const int time_stamp_count     = static_cast<int>(std::floor(state.emission_accumulator / interval));
        const int movement_stamp_count = static_cast<int>(std::floor(distance / spacing));

        // mix time-based and distance-based stamping so quick drags do not leave holes
        static constexpr int max_stamps_per_frame = 16;
        const int stamp_count = std::min(std::max(time_stamp_count, movement_stamp_count), max_stamps_per_frame);

        if (stamp_count <= 0) return;

        if (distance <= eps)
        {
            for (int i = 0; i < stamp_count; ++i) emit_stamp(*world_position);
        }
        else
        {
            for (int i = 1; i <= stamp_count; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(stamp_count);
                emit_stamp(lerp(previous_position, *world_position, t));
            }
        }

        state.last_stamp_world     = *world_position;
        state.emission_accumulator = std::fmod(state.emission_accumulator, interval);
    }
}
