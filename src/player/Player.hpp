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

	class Player final
	{
	public:
		Player() = default;
		~Player();

		Player(const Player&)                = delete;
		Player& operator=(const Player&)     = delete;
		Player(Player&&) noexcept            = delete;
		Player& operator=(Player&&) noexcept = delete;

		[[nodiscard]] 
		Result<void> create(b2WorldId world_id, vec2 spawn_position, vec2 planet_center, const PlayerConfig& config = {});
		void destroy();

		void teleport(vec2 world_position, vec2 planet_center);
		void refresh_grounded_state(vec2 planet_center);

		void prepare_for_physics_step(float fixed_step, vec2 planet_center, bool in_water);
		void sync_from_physics(vec2 planet_center);

		void draw_sf(sf::RenderTarget& target) const;

		[[nodiscard]] b2BodyId body() const;
		[[nodiscard]] bool valid() const;

		[[nodiscard]] vec2 world_position() const;
		[[nodiscard]] vec2 up_direction(vec2 planet_center) const;

		[[nodiscard]] 
		bool is_move_input_active() const;

		[[nodiscard]] 
		bool is_in_water() const noexcept { return in_water_; }
		void set_in_water(bool in_water) noexcept { in_water_ = in_water; }

	private:
		[[nodiscard]] Result<void> create_physics_body(vec2 spawn_position, float spawn_angle);
		[[nodiscard]] Result<void> create_capsule_shape();
		[[nodiscard]] Result<void> create_ground_sensor_shape();
		[[nodiscard]] Result<void> create_water_sensor_shape();

		void configure_capsule_drawable();
		void reset_ground_state(vec2 spawn_up);

		[[nodiscard]] bool sensor_detects_ground() const;
		[[nodiscard]] bool sensor_detects_water() const;

		[[nodiscard]] 
		std::optional<vec2> raycast_ground_normal(vec2 planet_center) const;

		[[nodiscard]] float movement_axis() const;
		[[nodiscard]] vec2 movement_direction(vec2 up_direction) const;

		void apply_horizontal_movement(float fixed_step, vec2 movement_direction, bool in_water);
		void try_jump(vec2 up_direction, bool jump_held, bool in_water);
		void apply_input(float fixed_step, vec2 planet_center, bool in_water);
		void apply_gravity(vec2 planet_center, bool in_water) const;
		void align_to_planet(vec2 planet_center) const;

		b2WorldId world_{ b2_nullWorldId };

		GameObject object_{};
		PlayerConfig config_{};

		b2ShapeId ground_sensor_shape_{ b2_nullShapeId };
		b2ShapeId water_sensor_shape_{ b2_nullShapeId };

		bool grounded_{ false };
		bool in_water_{ false };

		float jump_cooldown_timer_{ 0.0f };
		vec2 ground_normal_{ 0.0f, 1.0f };
	};
}
