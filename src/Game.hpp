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
		struct CameraSettings final
		{
			vec2 world_span{ 36.0f, 27.0f };
			float min_zoom{ 0.05f };
			float max_zoom{ 3.5f };
			float follow_threshold{ 3.25f };
			float follow_smoothing{ 10.0f };
			float recenter_smoothing{ 5.0f };
			float rotation_smoothing{ 7.5f };
		};

		struct CameraState final
		{
			float zoom{ 1.0f };
			bool initialized{ false };
			vec2 focus_world{ 0.0f, 0.0f };
			float rotation_radians{ 0.0f };
		};

		void update(float dt);
		void initialize_window();
		bool initialize_graphics();
		void initialize_world_state();
		void destroy_world();
		void destroy_graphics();
		void render_opengl();
		void render_sfml();
		void create_world();
		void create_player();
		void configure_input();
		void step_physics(float dt);
		void sync_camera_to_player(float dt);
		void update_terrain_editing(float dt);
		void export_current_chunk_field();
		void handle_tool_mouse_pressed(MouseButton button);
		void handle_resize(uvec2 size);
		void apply_viewport(uvec2 size) const;
		void update_world_view(uvec2 size);
		[[nodiscard]] sf::View make_ui_view() const;
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
		CameraSettings camera_settings_{};
		CameraState camera_state_{};

		tools::TerrainToolController terrain_tools_{};

		bool  gl_loaded_{ false };
		bool failed_{ false };
	};
}
