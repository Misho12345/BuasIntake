#include "pch.hpp"

#include "Player.hpp"

#include "platform/InputSystem.hpp"

namespace game::player
{
    namespace
    {
        struct GroundRayCastContext final
        {
            b2BodyId ignored_body{ b2_nullBodyId };
            bool     hit{ false };
            float    fraction{ 1.0f };
            vec2     normal{ 0.0f, 1.0f };
        };

        inline constexpr int sensor_overlap_capacity{ 32 };

        std::unique_ptr<sf::ConvexShape> make_capsule_drawable(
            const float       radius,
            const float       half_height,
            const std::size_t arc_segments = 12)
        {
            const auto top_arc_count = arc_segments + 1;
            auto       shape         = std::make_unique<sf::ConvexShape>(top_arc_count * 2);

            const float top_center_y    = half_height - radius;
            const float bottom_center_y = -half_height + radius;

            for (std::size_t i = 0; i <= arc_segments; ++i)
            {
                const float t     = static_cast<float>(i) / static_cast<float>(arc_segments);
                const float angle = pi * (1.0f - t);
                shape->setPoint(
                    i, {
                        std::cos(angle) * radius,
                        top_center_y + std::sin(angle) * radius
                    });
            }

            for (std::size_t i = 0; i <= arc_segments; ++i)
            {
                const float t     = static_cast<float>(i) / static_cast<float>(arc_segments);
                const float angle = -pi * t;
                shape->setPoint(
                    top_arc_count + i, {
                        std::cos(angle) * radius,
                        bottom_center_y + std::sin(angle) * radius
                    });
            }

            shape->setOrigin(shape->getLocalBounds().getCenter());
            return shape;
        }

        float ground_ray_cast_callback(
            const b2ShapeId shape_id,
            const b2Vec2,
            const b2Vec2 normal,
            const float fraction,
            void* context)
        {
            auto& ray_context = *static_cast<GroundRayCastContext*>(context);

            if (B2_ID_EQUALS(b2Shape_GetBody(shape_id), ray_context.ignored_body) ||
                b2Shape_IsSensor(shape_id))
                return -1.0f;

            ray_context.hit      = true;
            ray_context.fraction = fraction;
            ray_context.normal   = { normal.x, normal.y };

            return fraction;
        }
    }

    Player::~Player() { destroy(); }

    Result<void> Player::create(
        const b2WorldId     world_id,
        const vec2          spawn_position,
        const vec2          planet_center,
        const PlayerConfig& config)
    {
        destroy();
        if (!b2World_IsValid(world_id)) return fail("Player requires a valid Box2D world");

        world_  = world_id;
        config_ = config;

        const vec2  spawn_up    = normalize(spawn_position - planet_center);
        const float spawn_angle = dir_to_angle(spawn_up);
        object_.renderable      = make_capsule_drawable(config_.capsule_radius, config_.capsule_half_height);

        if (auto result = create_physics_body(spawn_position, spawn_angle); !result)
        {
            destroy();
            return fail(result.error());
        }

        if (auto result = create_capsule_shape(); !result)
        {
            destroy();
            return fail(result.error());
        }

        if (auto result = create_ground_sensor_shape(); !result)
        {
            destroy();
            return fail(result.error());
        }

        if (auto result = create_water_sensor_shape(); !result)
        {
            destroy();
            return fail(result.error());
        }

        configure_capsule_drawable();
        reset_ground_state(spawn_up);
        sync_from_physics(planet_center);

        return {};
    }

    Result<void> Player::create_physics_body(const vec2 spawn_position, const float spawn_angle)
    {
        b2BodyDef body_def         = b2DefaultBodyDef();
        body_def.type              = b2_dynamicBody;
        body_def.position          = { .x = spawn_position.x, .y = spawn_position.y };
        body_def.rotation          = b2MakeRot(spawn_angle);
        body_def.linearDamping     = 0.9f;
        body_def.angularDamping    = 8.0f;
        body_def.enableSleep       = false;
        body_def.isBullet          = true;
        body_def.allowFastRotation = false;
        body_def.name              = "player_capsule";

        object_.body = b2CreateBody(world_, &body_def);
        if (!b2Body_IsValid(object_.body)) return fail("Failed to create player body");

        return {};
    }

    Result<void> Player::create_capsule_shape()
    {
        b2ShapeDef shape_def           = b2DefaultShapeDef();
        shape_def.density              = 1.25f;
        shape_def.material.friction    = 0.95f;
        shape_def.material.restitution = 0.0f;

        const float     capsule_center_offset = std::max(config_.capsule_half_height - config_.capsule_radius, 0.01f);
        const b2Capsule capsule{
            .center1 = { .x = 0.0f, .y = -capsule_center_offset },
            .center2 = { .x = 0.0f, .y = capsule_center_offset },
            .radius  = config_.capsule_radius
        };

        if (!b2Shape_IsValid(b2CreateCapsuleShape(object_.body, &shape_def, &capsule)))
            return fail("Failed to create player capsule shape");
        return {};
    }

    Result<void> Player::create_ground_sensor_shape()
    {
        b2ShapeDef sensor_shape_def         = b2DefaultShapeDef();
        sensor_shape_def.isSensor           = true;
        sensor_shape_def.enableSensorEvents = true;
        sensor_shape_def.updateBodyMass     = false;
        sensor_shape_def.density            = 0.0f;

        // Keep this narrower than the capsule so touching a wall does not count as standing.
        const float     sensor_half_width  = config_.capsule_radius * 0.42f;
        constexpr float sensor_half_height = 0.08f;
        const float     sensor_offset_y    = -config_.capsule_half_height - sensor_half_height * 0.35f;
        const b2Polygon ground_sensor      = b2MakeOffsetBox(
            sensor_half_width,
            sensor_half_height,
            { .x = 0.0f, .y = sensor_offset_y },
            b2Rot_identity);

        ground_sensor_shape_ = b2CreatePolygonShape(object_.body, &sensor_shape_def, &ground_sensor);
        if (!b2Shape_IsValid(ground_sensor_shape_)) return fail("Failed to create player ground sensor shape");

        return {};
    }

    Result<void> Player::create_water_sensor_shape()
    {
        b2ShapeDef sensor_shape_def         = b2DefaultShapeDef();
        sensor_shape_def.isSensor           = true;
        sensor_shape_def.enableSensorEvents = true;
        sensor_shape_def.updateBodyMass     = false;
        sensor_shape_def.density            = 0.0f;

        // This one is larger on purpose so shallow ponds still register early.
        const float     sensor_half_width  = config_.capsule_radius * 0.92f;
        const float     sensor_half_height = config_.capsule_half_height * 0.92f;
        const b2Polygon water_sensor       = b2MakeOffsetBox(
            sensor_half_width,
            sensor_half_height,
            { .x = 0.0f, .y = 0.0f },
            b2Rot_identity);

        water_sensor_shape_ = b2CreatePolygonShape(object_.body, &sensor_shape_def, &water_sensor);
        if (!b2Shape_IsValid(water_sensor_shape_)) return fail("Failed to create player water sensor shape");

        return {};
    }

    void Player::configure_capsule_drawable()
    {
        auto& capsule_shape = dynamic_cast<sf::ConvexShape&>(*object_.renderable);
        capsule_shape.setFillColor(0xF29E4C_rgb);
        capsule_shape.setOutlineColor(0xFFF3D9_rgb);
        capsule_shape.setOutlineThickness(0.08f);
    }

    void Player::reset_ground_state(const vec2 spawn_up)
    {
        grounded_            = false;
        jump_cooldown_timer_ = 0.0f;
        ground_normal_       = spawn_up;
    }

    void Player::destroy()
    {
        if (b2Body_IsValid(object_.body)) b2DestroyBody(object_.body);

        object_              = GameObject{};
        world_               = b2_nullWorldId;
        ground_sensor_shape_ = b2_nullShapeId;
        water_sensor_shape_  = b2_nullShapeId;
        grounded_            = false;
        in_water_            = false;
        jump_cooldown_timer_ = 0.0f;
        ground_normal_       = { 0.0f, 1.0f };
    }

    void Player::refresh_grounded_state(const vec2 planet_center)
    {
        grounded_      = false;
        ground_normal_ = up_direction(planet_center);

        if (!valid() || !b2World_IsValid(world_)) return;
        const bool sensor_grounded = sensor_detects_ground();
        // The sensor is cheap, but the ray gives a better surface normal on slopes.
        const auto ray_ground_normal = raycast_ground_normal(planet_center);
        in_water_                    = sensor_detects_water();

        grounded_ = sensor_grounded || ray_ground_normal.has_value();
        if (ray_ground_normal.has_value()) ground_normal_ = *ray_ground_normal;
    }

    void Player::refresh_contact_state(const vec2 planet_center, const bool terrain_water)
    {
        refresh_grounded_state(planet_center);
        in_water_ = in_water_ || terrain_water;
    }

    bool Player::sensor_detects_ground() const
    {
        return sensor_detects_overlap(ground_sensor_shape_, false);
    }

    bool Player::sensor_detects_water() const
    {
        return sensor_detects_overlap(water_sensor_shape_, true);
    }

    bool Player::sensor_detects_overlap(const b2ShapeId sensor_shape, const bool target_is_sensor) const
    {
        if (!b2Shape_IsValid(sensor_shape)) return false;

        std::array<b2ShapeId, sensor_overlap_capacity> overlaps{};
        const int overlap_count = b2Shape_GetSensorOverlaps(
            sensor_shape,
            overlaps.data(),
            static_cast<int>(overlaps.size()));

        for (int i = 0; i < overlap_count; ++i)
        {
            const auto overlap_shape = overlaps[static_cast<std::size_t>(i)];
            if (!b2Shape_IsValid(overlap_shape)) continue;
            if (B2_ID_EQUALS(b2Shape_GetBody(overlap_shape), object_.body)) continue;
            if (b2Shape_IsSensor(overlap_shape) != target_is_sensor) continue;

            return true;
        }

        return false;
    }

    std::optional<vec2> Player::raycast_ground_normal(const vec2 planet_center) const
    {
        if (!valid() || !b2World_IsValid(world_)) return std::nullopt;

        const vec2 up = up_direction(planet_center);
        GroundRayCastContext ray_context{ .ignored_body = object_.body };
        const vec2 ray_origin = world_position() - up * (config_.capsule_half_height - config_.capsule_radius * 0.35f);
        const vec2 ray_translation = up * -(config_.capsule_radius + config_.ground_probe_distance);

        const b2QueryFilter filter = b2DefaultQueryFilter();
        b2World_CastRay(
            world_,
            to_b2(ray_origin),
            to_b2(ray_translation),
            filter,
            ground_ray_cast_callback,
            &ray_context);

        if (!ray_context.hit) return std::nullopt;

        const auto normal = normalize(ray_context.normal, up);
        if (normal.dot(up) < config_.ground_min_normal_dot) return std::nullopt;
        return normal;
    }

    void Player::prepare_for_physics_step(
        const float                  fixed_step,
        const vec2                   planet_center,
        const bool                   in_water)
    {
        if (!valid()) return;

        align_to_planet(planet_center);
        apply_gravity(planet_center, in_water);
        apply_input(fixed_step, planet_center, in_water);
    }

    void Player::sync_from_physics(const vec2 planet_center)
    {
        if (!valid()) return;

        align_to_planet(planet_center);
        refresh_grounded_state(planet_center);
        object_.sync_from_physics();
    }

    void Player::draw_sf(sf::RenderTarget& target) const { object_.draw_sf(target); }

    b2BodyId Player::body() const { return object_.body; }
    bool     Player::valid() const { return b2Body_IsValid(object_.body); }

    vec2 Player::world_position() const
    {
        if (!valid()) return object_.transformable.getPosition();
        return from_b2(b2Body_GetPosition(object_.body));
    }

    vec2 Player::up_direction(const vec2 planet_center) const { return normalize(world_position() - planet_center); }

    bool Player::is_move_input_active() const
    {
        return movement_axis() != 0.0f;
    }

    float Player::movement_axis() const
    {
        return (platform::InputSystem::is_pressed(Key::D) ? 1.0f : 0.0f) -
                (platform::InputSystem::is_pressed(Key::A) ? 1.0f : 0.0f);
    }

    vec2 Player::movement_direction(const vec2 up_direction) const
    {
        const vec2 right_direction{ up_direction.y, -up_direction.x };
        if (!grounded_) return right_direction;

        const float ground_alignment = ground_normal_.dot(up_direction);
        if (ground_alignment < config_.ground_min_normal_dot) return right_direction;

        // When grounded, move along the local surface instead of a flat world-space right vector.
        vec2 ground_tangent{ ground_normal_.y, -ground_normal_.x };
        if (ground_tangent.dot(right_direction) < 0.0f) ground_tangent = ground_tangent * -1.0f;

        return normalize(ground_tangent, right_direction);
    }

    void Player::apply_horizontal_movement(
        const float                  fixed_step,
        const vec2                   movement_direction,
        const bool                   in_water)
    {
        const vec2  current_velocity      = from_b2(b2Body_GetLinearVelocity(object_.body));
        const float current_tangent_speed = current_velocity.dot(movement_direction);
        const float move_axis             = movement_axis();
        const float player_mass           = b2Body_GetMass(object_.body);
        const float movement_scale        = in_water ? 0.68f : 1.0f;

        if (move_axis != 0.0f)
        {
            const float desired_tangent_speed = move_axis * config_.move_speed * movement_scale;
            const float max_speed_change = config_.move_acceleration * fixed_step * (in_water ? 0.60f : 1.0f);
            const float speed_change = std::clamp(
                desired_tangent_speed - current_tangent_speed,
                -max_speed_change,
                max_speed_change);

            if (std::abs(speed_change) <= 1e-4f) return;

            const vec2 impulse = movement_direction * (player_mass * speed_change);
            b2Body_ApplyLinearImpulseToCenter(object_.body, to_b2(impulse), true);
            return;
        }

        if (!grounded_ && !in_water) return;

        const float brake_speed = std::min(
            std::abs(current_tangent_speed),
            config_.ground_brake * fixed_step * (in_water ? 0.40f : 1.0f));

        if (brake_speed <= 1e-4f) return;

        const float direction     = current_tangent_speed > 0.0f ? -1.0f : 1.0f;
        const vec2  brake_impulse = movement_direction * (player_mass * brake_speed * direction);
        b2Body_ApplyLinearImpulseToCenter(object_.body, to_b2(brake_impulse), true);
    }

    void Player::try_jump(const vec2 up_direction, const bool jump_held, const bool in_water)
    {
        if (!jump_held) return;

        if (in_water)
        {
            const vec2 swim_force = up_direction * (b2Body_GetMass(object_.body) * config_.jump_speed * 1.9f);
            b2Body_ApplyForceToCenter(object_.body, to_b2(swim_force), true);
            return;
        }

        if (!grounded_ || jump_cooldown_timer_ > 0.0f) return;

        const vec2 jump_impulse = up_direction * (b2Body_GetMass(object_.body) * config_.jump_speed);
        b2Body_ApplyLinearImpulseToCenter(object_.body, to_b2(jump_impulse), true);
        grounded_            = false;
        ground_normal_       = up_direction;
        jump_cooldown_timer_ = config_.jump_cooldown;
    }

    void Player::apply_input(
        const float                  fixed_step,
        const vec2                   planet_center,
        const bool                   in_water)
    {
        if (!valid()) return;
        jump_cooldown_timer_ = std::max(jump_cooldown_timer_ - fixed_step, 0.0f);

        const vec2 up             = up_direction(planet_center);
        const auto move_direction = movement_direction(up);
        const bool jump_held      = platform::InputSystem::is_pressed(Key::W) ||
                                    platform::InputSystem::is_pressed(Key::Space);

        apply_horizontal_movement(fixed_step, move_direction, in_water);
        try_jump(up, jump_held, in_water);
    }

    void Player::apply_gravity(const vec2 planet_center, const bool in_water) const
    {
        if (!valid()) return;

        const float gravity_scale = in_water ? 0.22f : 1.0f;
        const vec2  gravity_force = up_direction(planet_center) * (
            -b2Body_GetMass(object_.body) *
            config_.gravity_acceleration *
            gravity_scale);

        b2Body_ApplyForceToCenter(object_.body, to_b2(gravity_force), true);
    }

    void Player::align_to_planet(const vec2 planet_center) const
    {
        if (!valid()) return;

        // The capsule always stays upright relative to the planet; spinning physics is not part of the movement here.
        const vec2  position      = world_position();
        const float target_angle  = dir_to_angle(up_direction(planet_center));
        const float current_angle = b2Rot_GetAngle(b2Body_GetRotation(object_.body));

        b2Body_SetAngularVelocity(object_.body, 0.0f);
        if (std::abs(shortest_angle_delta(current_angle, target_angle)) <= 1e-4f) return;

        b2Body_SetTransform(object_.body, to_b2(position), b2MakeRot(target_angle));
    }
}
