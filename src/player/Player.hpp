#pragma once

#include "pch.hpp"

#include "GameObject.hpp"

namespace game::player
{
    struct PlayerConfig final
    {
        float capsule_radius{ 0.4f };
        float capsule_half_height{ 0.95f };
        float spawn_air_clearance{ 2.0f };
        float gravity_acceleration{ 36.0f };
        float move_speed{ 9.5f };
        float move_acceleration{ 95.0f };
        float ground_brake{ 40.0f };
        float jump_speed{ 11.5f };
        float ground_probe_distance{ 0.22f };
        float ground_min_normal_dot{ 0.35f };
        float jump_cooldown{ 0.14f };
    };

    // box2d-backed character controller for walking around the curved planet surface
    // it owns the body sensors and movement rules while Game decides when to refresh contacts and step physics
    class Player final
    {
    public:
        Player() = default;
        ~Player();

        Player(const Player&)                = delete;
        Player& operator=(const Player&)     = delete;
        Player(Player&&) noexcept            = delete;
        Player& operator=(Player&&) noexcept = delete;

        // create does the whole spawn body and sensor setup
        Result<void> create(
            b2WorldId           world_id,
            vec2                spawn_position,
            vec2                planet_center,
            const PlayerConfig& config = {});
        void destroy();

        // this refreshes both walkable ground state and water overlap state because movement logic needs both every frame
        void refresh_grounded_state(vec2 planet_center);
        void refresh_contact_state(vec2 planet_center, bool terrain_water);

        // this is the per step movement entry point used before each box2d step
        // it lines the player up to the planet then applies gravity and input in that order
        void prepare_for_physics_step(
            float fixed_step,
            vec2  planet_center,
            bool  in_water);

        void sync_from_physics(vec2 planet_center);

        void draw_sf(sf::RenderTarget& target) const;

        b2BodyId body() const;
        bool     valid() const;

        vec2 world_position() const;
        vec2 up_direction(vec2 planet_center) const;

        bool is_move_input_active() const;
        bool is_in_water() const { return in_water_; }

    private:
        Result<void> create_physics_body(vec2 spawn_position, float spawn_angle);
        Result<void> create_capsule_shape();
        Result<void> create_ground_sensor_shape();
        Result<void> create_water_sensor_shape();

        void configure_capsule_drawable();
        void reset_ground_state(vec2 spawn_up);

        bool sensor_detects_ground() const;
        bool sensor_detects_water() const;
        bool sensor_detects_overlap(b2ShapeId sensor_shape, bool target_is_sensor) const;

        // this is the more reliable ground probe than the overlap sensor because it gives us a usable surface normal
        std::optional<vec2> raycast_ground_normal(vec2 planet_center) const;

        float movement_axis() const;
        vec2  movement_direction(vec2 up_direction) const;

        // this does the actual impulse math for walking and braking and it is where the grounded vs water behavior splits
        void apply_horizontal_movement(
            float fixed_step,
            vec2  movement_direction,
            bool  in_water);

        void try_jump(float fixed_step, vec2 up_direction, bool jump_held, bool in_water);
        void apply_input(float fixed_step, vec2 planet_center, bool in_water);
        void apply_gravity(vec2 planet_center, bool in_water) const;
        void align_to_planet(vec2 planet_center) const;

        b2WorldId world_{ b2_nullWorldId };

        GameObject   object_{};
        PlayerConfig config_{};

        b2ShapeId ground_sensor_shape_{ b2_nullShapeId };
        b2ShapeId water_sensor_shape_{ b2_nullShapeId };

        bool grounded_{ false };
        bool in_water_{ false };

        float jump_cooldown_timer_{ 0.0f };
        vec2  ground_normal_{ 0.0f, 1.0f };
    };
}
