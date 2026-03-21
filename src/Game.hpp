#pragma once
#include "pch.hpp"

#include "gfx/ColorMeshRenderer.hpp"
#include "terrain/TerrainChunk.hpp"

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
		void apply_player_input();
		void step_physics(float dt);
		void update_world_view(uvec2 size);

		GameSettings settings_{};

		sf::RenderWindow window_{};
		sf::Clock        clock_{};
		sf::View         world_view_{};

		b2WorldId world_{ b2_nullWorldId };
		b2BodyId  player_body_{ b2_nullBodyId };
		std::optional<gfx::ColorMeshRenderer> terrain_renderer_{ std::nullopt };
		std::optional<terrain::TerrainChunk> terrain_{ std::nullopt };

		sf::CircleShape player_shape_{};

		float player_radius_{ 0.35f };
		float physics_accumulator_{ 0.0f };
		bool  gl_loaded_{ false };

		bool failed_{ false };
	};
}
