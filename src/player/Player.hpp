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

		void create(b2WorldId world_id, vec2 spawn_position, vec2 planet_center, const PlayerConfig& config = {});
		void destroy();
		void refresh_grounded_state(vec2 planet_center);
		void prepare_for_physics_step(float fixed_step, vec2 planet_center);
		void sync_from_physics(vec2 planet_center);
		void draw_gl(const sf::View& view) const;
		void draw_sf(sf::RenderTarget& target) const;

		[[nodiscard]] b2BodyId body() const;
		[[nodiscard]] bool valid() const;
		[[nodiscard]] vec2 world_position() const;
		[[nodiscard]] vec2 up_direction(vec2 planet_center) const;
		[[nodiscard]] bool is_move_input_active() const;

	private:
		void apply_input(float fixed_step, vec2 planet_center);
		void apply_gravity(vec2 planet_center) const;
		void align_to_planet(vec2 planet_center) const;

		b2WorldId world_{ b2_nullWorldId };
		GameObject object_{};
		PlayerConfig config_{};
		b2ShapeId body_shape_{ b2_nullShapeId };
		b2ShapeId ground_sensor_shape_{ b2_nullShapeId };
		bool grounded_{ false };
		float jump_cooldown_timer_{ 0.0f };
		vec2 ground_normal_{ 0.0f, 1.0f };
	};
}
