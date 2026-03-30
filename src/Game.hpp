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
		void apply_player_input() const;
		void configure_input();
		void step_physics(float dt);
		void sync_camera_to_player();
		void update_terrain_editing(float dt);
		void emit_terrain_tool_stamps(MouseButton button, const TerrainToolConfig& config, TerrainToolState& state, float dt);
		void update_world_view(uvec2 size);
		[[nodiscard]] std::optional<vec2> terrain_tool_hit_world_position() const;
		[[nodiscard]] vec2 mouse_world_position() const;

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
		float min_camera_zoom_{ 0.05f };
		float max_camera_zoom_{ 3.5f };
		TerrainToolConfig dig_tool_{ 2.25f, -0.85f, 60.0f, 2.45f, 2.8f };
		TerrainToolConfig place_tool_{ 1.05f, 0.8f, 60.0f, 1.45f, 1.5f };
		TerrainToolState dig_tool_state_{};
		TerrainToolState place_tool_state_{};
		bool  gl_loaded_{ false };

		bool failed_{ false };
	};
}
