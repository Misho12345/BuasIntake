#pragma once

#include "pch.hpp"

#include "platform/InputSystem.hpp"
#include "player/Player.hpp"
#include "render/CameraController.hpp"
#include "render/WorldRenderer.hpp"
#include "terrain/PlanetTerrain.hpp"
#include "tools/TerrainToolController.hpp"
#include "ui/GoalProgressHud.hpp"
#include "ui/InventoryHud.hpp"
#include "world/World.hpp"
#include "world/PlanetRestorationGoal.hpp"

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
        Game(const Game&)                = delete;
        Game& operator=(const Game&)     = delete;
        Game(Game&&) noexcept            = delete;
        Game& operator=(Game&&) noexcept = delete;

        static void initialize(GameSettings settings);
        static void run();
        static void shutdown();
        static void quit();

    private:
        Game() = default;
        ~Game();

        static Game& instance();

        void initialize_impl(GameSettings settings);
        void run_impl();
        void shutdown_impl();

        void update(float dt);
        void handle_frame_input();
        void handle_scroll_input();
        bool handle_global_shortcuts();
        void handle_modal_input();
        void handle_gameplay_input();

        // window and gl setup are split out because if any one of these dies we want a clean early exit
        Result<void> initialize_window();
        Result<void> initialize_graphics();
        Result<void> initialize_world_state();

        void destroy_world();
        void destroy_graphics();

        void render_opengl();
        void render_sfml();

        Result<void> create_world();

        // fixed_update is the physics side and variable_update is the game logic side
        // keeping those separate avoids mixing box2d stepping with frame rate dependent work
        void fixed_update(float dt);
        void variable_update(float dt);
        void update_win_condition();
        void sync_camera_to_player(float dt);
        void update_terrain_editing(float dt);

        void handle_tool_mouse_pressed(MouseButton button);
        void handle_resize(uvec2 size);
        void apply_viewport(uvec2 size) const;

        sf::View make_ui_view() const;

        // this bundles the live world state into one little packet so tool code does not have to know about the whole game object
        std::optional<tools::TerrainToolContext> terrain_tool_context();

        vec2 mouse_world_position() const;
        bool is_player_move_input_active() const;

        GameSettings settings_{};

        sf::RenderWindow window_{};
        sf::Clock        clock_{};

        b2WorldId world_{ b2_nullWorldId };

        world::World                 world_state_{};
        world::PlanetRestorationGoal restoration_goal_{};
        player::PlayerConfig         player_config_{};

        float                    physics_accumulator_{ 0.0f };
        render::CameraController camera_{};

        render::WorldRenderer        world_renderer_{};
        tools::TerrainToolController terrain_tools_{};
        ui::InventoryHud             inventory_hud_{};
        ui::GoalProgressHud          goal_hud_{};
        float                        debug_validation_timer_{ 0.0f };

        bool initialized_{ false };
        bool gl_loaded_{ false };
        bool failed_{ false };
    };
}
