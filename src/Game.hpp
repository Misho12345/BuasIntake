#pragma once
#include "pch.hpp"

#include "player/Player.hpp"
#include "terrain/PlanetTerrain.hpp"
#include "tools/TerrainToolController.hpp"

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
		void update(float dt);
		void render_opengl() const;
		void render_sfml();
		void create_world();
		void create_player();
		void configure_input();
		void step_physics(float dt);
		void sync_camera_to_player(float dt);
		void update_terrain_editing(float dt);
		void export_current_chunk_field();
		void handle_water_input(MouseButton button);
		void update_world_view(uvec2 size);
		std::optional<tools::TerrainToolContext> terrain_tool_context();
		vec2 mouse_world_position() const;
		vec2 player_up_direction() const;
		bool is_player_move_input_active() const;

		GameSettings settings_{};

		sf::RenderWindow window_{};
		sf::Clock        clock_{};
		sf::View         world_view_{};

		b2WorldId world_{ b2_nullWorldId };

		std::optional<terrain::PlanetTerrain> terrain_{ std::nullopt };

		player::Player player_{};
		player::PlayerConfig player_config_{};

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

		tools::TerrainToolController terrain_tools_{};

		bool  gl_loaded_{ false };
		bool failed_{ false };
	};
}
