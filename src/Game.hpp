#pragma once
#include "pch.hpp"

#include "GameObject.hpp"
#include "terrain/PlanetTerrain.hpp"

namespace game
{
	struct GameSettings final
	{
		std::string title;
		uvec2       win_size;
		sf::Color   clear_color;
	};

	class Game final
	{
	public:
		explicit Game(GameSettings settings);
		~Game();

		Game(const Game&)                = delete;
		Game& operator=(const Game&)     = delete;
		Game(Game&&) noexcept            = delete;
		Game& operator=(Game&&) noexcept = delete;

		void run();

		static void quit();

	private:
		struct TerrainToolConfig final
		{
			float radius{ 1.2f };
			float signed_strength_per_stamp{ 0.0f };
			float stamps_per_second{ 30.0f };
			float spacing_factor{ 0.5f };
			float falloff_exponent{ 1.8f };
		};

		struct TerrainToolState final
		{
			float emission_accumulator{ 0.0f };
			std::optional<vec2> last_stamp_world{ std::nullopt };
		};

		void update(float dt);
		void render_opengl() const;
		void render_sfml();
		void create_world();
		void create_player();
		void apply_player_input(float fixed_step);
		void apply_player_gravity() const;
		void update_player_grounded_state();
		void align_player_to_planet() const;
		void configure_input();
		void step_physics(float dt);
		void sync_camera_to_player(float dt);
		void update_terrain_editing(float dt);
		void emit_terrain_tool_stamps(MouseButton button, const TerrainToolConfig& config, TerrainToolState& state, float dt);
		void update_world_view(uvec2 size);
		[[nodiscard]] std::optional<vec2> terrain_tool_hit_world_position() const;
		[[nodiscard]] vec2 mouse_world_position() const;
		[[nodiscard]] vec2 player_world_position() const;
		[[nodiscard]] vec2 player_up_direction() const;
		[[nodiscard]] bool is_player_move_input_active() const;

		GameSettings settings_{};

		sf::RenderWindow window_{};
		sf::Clock        clock_{};
		sf::View         world_view_{};

		b2WorldId world_{ b2_nullWorldId };
		std::optional<terrain::PlanetTerrain> terrain_{ std::nullopt };
		GameObject player_{};
		b2ShapeId player_shape_{ b2_nullShapeId };
		b2ShapeId player_ground_sensor_shape_{ b2_nullShapeId };

		float player_capsule_radius_{ 0.4f };
		float player_capsule_half_height_{ 0.95f };
		float player_spawn_air_clearance_{ 2.0f };
		float player_gravity_acceleration_{ 36.0f };
		float player_move_speed_{ 9.5f };
		float player_move_acceleration_{ 95.0f };
		float player_ground_brake_{ 40.0f };
		float player_jump_speed_{ 11.5f };
		float player_ground_probe_distance_{ 0.22f };
		float player_ground_min_normal_dot_{ 0.35f };
		float player_jump_cooldown_{ 0.14f };
		bool  player_grounded_{ false };
		float jump_cooldown_timer_{ 0.0f };
		vec2  player_ground_normal_{ 0.0f, 1.0f };
		float physics_accumulator_{ 0.0f };
		vec2 camera_world_span_{ 36.0f, 27.0f };
		float camera_zoom_{ 1.0f };
		float min_camera_zoom_{ 0.05f };
		float max_camera_zoom_{ 3.5f };
		bool  camera_initialized_{ false };
		vec2 camera_focus_world_{ 0.0f, 0.0f };
		float camera_rotation_radians_{ 0.0f };
		float camera_follow_threshold_{ 3.25f };
		float camera_follow_smoothing_{ 10.0f };
		float camera_recenter_smoothing_{ 5.0f };
		float camera_rotation_smoothing_{ 7.5f };
		TerrainToolConfig dig_tool_{ 2.25f, -0.85f, 60.0f, 2.45f, 2.8f };
		TerrainToolConfig place_tool_{ 1.05f, 0.8f, 60.0f, 1.45f, 1.5f };
		TerrainToolState dig_tool_state_{};
		TerrainToolState place_tool_state_{};
		bool  gl_loaded_{ false };

		bool failed_{ false };
	};
}
