#include "pch.hpp"

#include "Game.hpp"

#include "ui/UiFont.hpp"

#include "platform/InputSystem.hpp"
#include "terrain/PlanetTerrain.hpp"

namespace game
{
    using platform::InputSystem;

    namespace
    {
        #ifndef NDEBUG
        void gl_debug_callback(
            const GLenum source,
            const GLenum type,
            const GLuint /*id*/,
            const GLenum severity,
            const GLsizei /*length*/,
            const GLchar* message,
            const void* /*user_param*/)
        {
            if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

            auto source_name = [](const GLenum value) -> std::string_view
            {
                switch (value)
                {
                    case GL_DEBUG_SOURCE_API: return "api";
                    case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "window";
                    case GL_DEBUG_SOURCE_SHADER_COMPILER: return "shader";
                    case GL_DEBUG_SOURCE_THIRD_PARTY: return "third_party";
                    case GL_DEBUG_SOURCE_APPLICATION: return "application";
                    default: return "other";
                }
            };

            auto type_name = [](const GLenum value) -> std::string_view
            {
                switch (value)
                {
                    case GL_DEBUG_TYPE_ERROR: return "error";
                    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "deprecated";
                    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "undefined";
                    case GL_DEBUG_TYPE_PORTABILITY: return "portability";
                    case GL_DEBUG_TYPE_PERFORMANCE: return "performance";
                    case GL_DEBUG_TYPE_MARKER: return "marker";
                    case GL_DEBUG_TYPE_PUSH_GROUP: return "push_group";
                    case GL_DEBUG_TYPE_POP_GROUP: return "pop_group";
                    default: return "other";
                }
            };

            auto severity_name = [](const GLenum value) -> std::string_view
            {
                switch (value)
                {
                    case GL_DEBUG_SEVERITY_HIGH: return "high";
                    case GL_DEBUG_SEVERITY_MEDIUM: return "medium";
                    case GL_DEBUG_SEVERITY_LOW: return "low";
                    default: return "other";
                }
            };

            const std::string_view safe_message = message != nullptr ? std::string_view{ message } : std::string_view{};
            if (severity == GL_DEBUG_SEVERITY_HIGH)
            {
                Log::error(
                    "OpenGL [{}:{}:{}] {}",
                    source_name(source),
                    type_name(type),
                    severity_name(severity),
                    safe_message);
            }
            else
            {
                Log::warn(
                    "OpenGL [{}:{}:{}] {}",
                    source_name(source),
                    type_name(type),
                    severity_name(severity),
                    safe_message);
            }
        }
        #endif
    }

    Game::~Game() = default;

    void Game::initialize(GameSettings settings) { instance().initialize_impl(std::move(settings)); }

    void Game::run() { instance().run_impl(); }
    void Game::shutdown() { instance().shutdown_impl(); }

    Game& Game::instance()
    {
        static Game game;
        return game;
    }


    void Game::initialize_impl(GameSettings settings)
    {
        if (initialized_)
        {
            Log::error("Game has already been initialized");
            failed_ = true;
            return;
        }

        settings_    = std::move(settings);
        initialized_ = true;

        auto result_err_check = [](const Result<void>& result) -> bool
        {
            if (!result) Log::error(result.error());
            return !result;
        };

        if (result_err_check(initialize_window()) ||
            result_err_check(initialize_graphics()) ||
            result_err_check(ui::initialize_ui_font()) ||
            result_err_check(terrain_tools_.initialize_ui_assets()) ||
            result_err_check(inventory_hud_.initialize_assets()) ||
            result_err_check(world_renderer_.initialize_assets()) ||
            result_err_check(goal_hud_.initialize_assets()) ||
            result_err_check(tutorial_.initialize_assets()) ||
            result_err_check(initialize_world_state()))
        {
            failed_ = true;
        }
    }

    // this is the top level frame loop and the order here is not accidental
    // input first, then simulation, then gl, then sfml because the sfml pass resets shared gl state
    void Game::run_impl()
    {
        if (!initialized_) return;
        if (failed_) return;

        while (window_.isOpen())
        {
            InputSystem::begin_frame();
            const auto [
                should_close,
                resized,
                new_size
            ] = InputSystem::update(window_);

            if (should_close) break;
            if (resized) handle_resize(new_size);
            handle_frame_input();

            const float dt = clock_.restart().asSeconds();
            update(dt);

            window_.clear(settings_.clear_color);

            render_opengl();
            window_.resetGLStates();
            render_sfml();

            window_.display();
        }
    }

    void Game::shutdown_impl()
    {
        destroy_world();
        destroy_graphics();

        if (window_.isOpen()) window_.close();
        initialized_ = false;
        failed_      = false;
    }


    // physics gets the real dt, while variable update is clamped so one frame hitch does not throw off camera and tool feel
    void Game::update(const float dt)
    {
        fixed_update(dt);
        variable_update(std::min(dt, 1.0f / 15.0f));

        #ifndef NDEBUG
        debug_validation_timer_ = std::max(0.0f, debug_validation_timer_ - dt);
        if (debug_validation_timer_ <= 0.0f)
        {
            world_state_.validate();
            debug_validation_timer_ = 2.0f;
        }
        #endif
    }

    Result<void> Game::initialize_window()
    {
        window_ = sf::RenderWindow{
            sf::VideoMode{ settings_.win_size },
            settings_.title,
            sf::State::Windowed,
            {
                .depthBits      = 24,
                .stencilBits    = 8,
                .majorVersion   = 4,
                .minorVersion   = 6,
                .attributeFlags = sf::ContextSettings::Default,
            }
        };

        window_.setFramerateLimit(1000);
        window_.setKeyRepeatEnabled(false);
        if (!window_.isOpen()) return fail("Failed to create the main render window");

        return {};
    }

    Result<void> Game::initialize_graphics()
    {
        if (!window_.setActive(true)) return fail("Failed to activate the OpenGL context");
        if (gladLoaderLoadGL() == 0) return fail("Failed to initialize GLAD");

        gl_loaded_ = true;
        apply_viewport(settings_.win_size);

        #ifndef NDEBUG
        if (GLAD_GL_VERSION_4_3)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(gl_debug_callback, nullptr);
        }
        #endif

        return {};
    }

    Result<void> Game::initialize_world_state()
    {
        TRY(create_world());
        TRY(world_state_.initialize(world_, player_config_));

        camera_.set_world_span(world_state_.terrain().chunk_size() * 1.75f);

        camera_.update_view_size(settings_.win_size);
        camera_.sync_to_player(world_state_, 0.0f, is_player_move_input_active());

        #ifndef NDEBUG
        world_state_.validate();
        debug_validation_timer_ = 2.0f;
        #endif

        return {};
    }


    void Game::destroy_world()
    {
        world_state_.destroy();

        if (!b2World_IsValid(world_)) return;

        b2DestroyWorld(world_);
        world_ = b2_nullWorldId;
    }

    void Game::destroy_graphics()
    {
        if (!gl_loaded_) return;

        inventory_hud_.destroy_graphics_resources();
        goal_hud_.destroy_graphics_resources();
        tutorial_.destroy_graphics_resources();
        world_renderer_.destroy_graphics_resources();
        terrain_tools_.destroy_graphics_resources();
        ui::destroy_ui_font();
        gfx::Shader::clear_cache();
        gladLoaderUnloadGL();
        gl_loaded_ = false;
    }


    void Game::render_opengl()
    {
        // draw the GL world first; the SFML pass that follows resets shared GL state
        if (const auto context = terrain_tool_context();
            context.has_value())
        {
            world_renderer_.draw(world_state_, terrain_tools_.active_water_preview(*context), camera_.view());
            return;
        }

        world_renderer_.draw(world_state_, std::nullopt, camera_.view());
    }

    void Game::render_sfml()
    {
        window_.setView(camera_.view());
        if (const auto context = terrain_tool_context();
            context.has_value())
            terrain_tools_.draw_targeting_overlay(window_, *context);

        if (world_state_.ready()) world_state_.player().draw_sf(window_);

        window_.setView(make_ui_view());
        if (world_state_.ready()) inventory_hud_.draw(window_, world_state_.resources().hud_state());
        terrain_tools_.draw_ui(window_);
        if (world_state_.ready()) goal_hud_.draw(window_, restoration_goal_);
        tutorial_.draw(window_);
    }


    Result<void> Game::create_world()
    {
        b2WorldDef world_def = b2DefaultWorldDef();
        world_def.gravity    = { .x = 0.0f, .y = 0.0f };
        world_               = b2CreateWorld(&world_def);
        if (!b2World_IsValid(world_)) return fail("Failed to create the Box2D world");
        return {};
    }

    // handle frame input in priority order
    // modal UI gets input before gameplay while a menu is open
    void Game::handle_frame_input()
    {
        if (restoration_goal_.completed()) return;

        handle_scroll_input();
        if (tutorial_.active())
        {
            handle_tutorial_input();
            return;
        }

        if (handle_global_shortcuts()) return;

        if (terrain_tools_.upgrade_menu_open())
        {
            handle_modal_input();
            return;
        }

        handle_gameplay_input();
    }

    void Game::handle_scroll_input()
    {
        if (const float scroll_delta = InputSystem::mouse_wheel_delta();
            scroll_delta != 0.0f)
        {
            // Plain scroll swaps tools; Ctrl + scroll is for camera zoom
            if (InputSystem::is_pressed(Key::LControl) || InputSystem::is_pressed(Key::RControl))
            {
                camera_.zoom_by_scroll(scroll_delta);
                camera_.update_view_size(window_.getSize());
                camera_.sync_to_player(world_state_, 0.0f, is_player_move_input_active());
            }
            else terrain_tools_.handle_scroll(scroll_delta);
        }
    }

    bool Game::handle_global_shortcuts()
    {
        if (InputSystem::just_pressed(Key::E))
        {
            terrain_tools_.toggle_upgrade_menu();
            return true;
        }

        // added these "hacks" for the sake of the testers, so it's easier for them
        if (InputSystem::just_pressed(Key::Enter))
        {
            if (const auto context = terrain_tool_context();
                context.has_value())
                terrain_tools_.handle_instant_resource_shortcut(*context);

            return true;
        }

        if (InputSystem::just_pressed(Key::U))
        {
            terrain_tools_.handle_instant_upgrade_shortcut();
            return true;
        }

        if (InputSystem::just_pressed(Key::Escape))
        {
            terrain_tools_.close_upgrade_menu();
            terrain_tools_.cancel_active_interaction();
            return true;
        }

        return false;
    }

    void Game::handle_modal_input()
    {
        if (InputSystem::just_pressed(MouseButton::Left))
        {
            if (const auto context = terrain_tool_context();
                context.has_value())
            {
                const auto pixel_position = sf::Mouse::getPosition(window_);
                const auto ui_position    = window_.mapPixelToCoords(pixel_position, make_ui_view());
                terrain_tools_.handle_upgrade_menu_click(*context, ui_position, window_.getSize());
            }
        }
    }

    void Game::handle_tutorial_input()
    {
        if (InputSystem::just_pressed(Key::Escape))
        {
            tutorial_.close();
            return;
        }

        if (InputSystem::just_pressed(Key::Left) ||
            InputSystem::just_pressed(Key::Backspace))
        {
            tutorial_.previous_slide();
            return;
        }

        if (InputSystem::just_pressed(Key::Right) ||
            InputSystem::just_pressed(Key::Space) ||
            InputSystem::just_pressed(Key::Enter))
        {
            tutorial_.next_slide();
            return;
        }

        if (InputSystem::just_pressed(MouseButton::Left))
        {
            const auto pixel_position = sf::Mouse::getPosition(window_);
            const auto ui_position    = window_.mapPixelToCoords(pixel_position, make_ui_view());
            tutorial_.handle_click(ui_position, window_.getSize());
        }
    }

    void Game::handle_gameplay_input()
    {
        if (InputSystem::just_pressed(MouseButton::Left)) handle_tool_mouse_pressed(MouseButton::Left);
        if (InputSystem::just_pressed(MouseButton::Right)) handle_tool_mouse_pressed(MouseButton::Right);
    }


    // runs the deterministic side of the game
    // Step through the accumulator at a fixed rate and refresh contact state around each step.
    // refreshing each step keeps contact state stable when terrain changes under the player
    void Game::fixed_update(const float dt)
    {
        if (!b2World_IsValid(world_) || !world_state_.ready()) return;
        if (restoration_goal_.completed()) return;
        physics_accumulator_ = std::min(physics_accumulator_ + dt, 0.025f);

        static constexpr float fixed_step    = 1.0f / 60.0f;
        static constexpr int   sub_steps     = 4;
        auto&                  player        = world_state_.player();
        const auto&            terrain       = world_state_.terrain();
        const vec2             planet_center = terrain.planet_center();

        auto refresh_player_state = [&player, &terrain, planet_center]
        {
            // sample contacts around each physics step so jump and swim state stays in sync with terrain edits
            player.refresh_contact_state(planet_center, terrain.contains_water_volume(player.world_position()));
        };

        refresh_player_state();

        while (physics_accumulator_ >= fixed_step)
        {
            const bool in_water = player.is_in_water();
            player.prepare_for_physics_step(fixed_step, planet_center, in_water);
            b2World_Step(world_, fixed_step, sub_steps);
            refresh_player_state();
            physics_accumulator_ -= fixed_step;
        }

        player.sync_from_physics(planet_center);
        player.refresh_contact_state(planet_center, terrain.contains_water_volume(player.world_position()));
    }

    // frame-rate-dependent updates run in this order on purpose
    // tool edits can change terrain, then world systems react, then the win check runs, then the camera follows the final result
    void Game::variable_update(const float dt)
    {
        if (!restoration_goal_.completed()) update_terrain_editing(dt);
        world_state_.update(dt);
        update_win_condition();
        sync_camera_to_player(dt);
    }

    void Game::update_win_condition()
    {
        if (!restoration_goal_.update(world_state_)) return;

        terrain_tools_.close_upgrade_menu();
        terrain_tools_.cancel_active_interaction();
        Log::info("Planet restored: {:.0f}% of the surface is green",
                  restoration_goal_.green_surface_coverage() * 100.0f);
    }

    void Game::sync_camera_to_player(const float dt)
    {
        camera_.sync_to_player(world_state_, dt, is_player_move_input_active());
    }

    void Game::update_terrain_editing(const float dt)
    {
        if (const auto context = terrain_tool_context();
            context.has_value())
            terrain_tools_.update(*context, dt);
    }

    void Game::handle_tool_mouse_pressed(const MouseButton button)
    {
        if (const auto context = terrain_tool_context();
            context.has_value())
            terrain_tools_.handle_mouse_pressed(*context, button);
    }

    void Game::handle_resize(const uvec2 size)
    {
        apply_viewport(size);
        camera_.update_view_size(size);
        camera_.sync_to_player(world_state_, 0.0f, is_player_move_input_active());
    }

    void Game::apply_viewport(const uvec2 size) const
    {
        if (size.x == 0 || size.y == 0) return;
        glViewport(0, 0, static_cast<std::int32_t>(size.x), static_cast<std::int32_t>(size.y));
    }

    // keep tool-facing state in one context instead of having every tool reach through Game
    std::optional<tools::TerrainToolContext> Game::terrain_tool_context()
    {
        if (!b2World_IsValid(world_) || !world_state_.ready()) return std::nullopt;

        return tools::TerrainToolContext{
            .world                 = world_,
            .terrain               = &world_state_.terrain(),
            .resources             = &world_state_.resources(),
            .water                 = &world_state_.water(),
            .input                 = &InputSystem::instance(),
            .player_body           = world_state_.player().body(),
            .player_world_position = world_state_.player().world_position(),
            .mouse_world_position  = mouse_world_position()
        };
    }

    vec2 Game::mouse_world_position() const { return camera_.mouse_world_position(window_); }

    sf::View Game::make_ui_view() const
    {
        const auto window_size = static_cast<vec2>(window_.getSize());

        return {
            window_size * 0.5f,
            window_size
        };
    }

    bool Game::is_player_move_input_active() const
    {
        return world_state_.ready() && world_state_.player().is_move_input_active();
    }
}
