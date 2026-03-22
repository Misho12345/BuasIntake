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
		void update(float dt);
		void render_opengl() const;
		void render_sfml();
		void create_world();
		void create_player();
		void apply_player_input() const;
		void configure_input();
		void step_physics(float dt);
		void sync_camera_to_player();
		void update_world_view(uvec2 size);

		GameSettings settings_{};

		sf::RenderWindow window_{};
		sf::Clock        clock_{};
		sf::View         world_view_{};

		b2WorldId world_{ b2_nullWorldId };
		std::optional<terrain::PlanetTerrain> terrain_{ std::nullopt };
		GameObject player_{};

		float player_radius_{ 0.35f };
		float physics_accumulator_{ 0.0f };
		vec2 camera_world_span_{ 36.0f, 27.0f };
		float camera_zoom_{ 1.0f };
		float min_camera_zoom_{ 0.35f };
		float max_camera_zoom_{ 3.5f };
		bool  gl_loaded_{ false };

		bool failed_{ false };
	};
}
