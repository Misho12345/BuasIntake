#include "pch.hpp"

#include "Game.hpp"

namespace game
{
    namespace
    {
        Game* instance{ nullptr };

#ifndef NDEBUG
        void gl_debug_callback(const GLenum source,
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
                    case GL_DEBUG_SOURCE_API:
                        return "api";
                    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
                        return "window";
                    case GL_DEBUG_SOURCE_SHADER_COMPILER:
                        return "shader";
                    case GL_DEBUG_SOURCE_THIRD_PARTY:
                        return "third_party";
                    case GL_DEBUG_SOURCE_APPLICATION:
                        return "application";
                    default:
                        return "other";
                }
            };

            auto type_name = [](const GLenum value) -> std::string_view
            {
                switch (value)
                {
                    case GL_DEBUG_TYPE_ERROR:
                        return "error";
                    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
                        return "deprecated";
                    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
                        return "undefined";
                    case GL_DEBUG_TYPE_PORTABILITY:
                        return "portability";
                    case GL_DEBUG_TYPE_PERFORMANCE:
                        return "performance";
                    case GL_DEBUG_TYPE_MARKER:
                        return "marker";
                    case GL_DEBUG_TYPE_PUSH_GROUP:
                        return "push_group";
                    case GL_DEBUG_TYPE_POP_GROUP:
                        return "pop_group";
                    default:
                        return "other";
                }
            };

            auto severity_name = [](const GLenum value) -> std::string_view
            {
                switch (value)
                {
                    case GL_DEBUG_SEVERITY_HIGH:
                        return "high";
                    case GL_DEBUG_SEVERITY_MEDIUM:
                        return "medium";
                    case GL_DEBUG_SEVERITY_LOW:
                        return "low";
                    default:
                        return "other";
                }
            };

            const std::string_view safe_message = message != nullptr ? std::string_view{ message } : std::string_view{};
            if (severity == GL_DEBUG_SEVERITY_HIGH) Log::error("OpenGL [{}:{}:{}] {}", source_name(source), type_name(type), severity_name(severity), safe_message);
            else Log::warn("OpenGL [{}:{}:{}] {}", source_name(source), type_name(type), severity_name(severity), safe_message);
        }
#endif
    }

    Game::Game(GameSettings settings) : settings_{ std::move(settings) }
    {
        if (instance != nullptr)
        {
            Log::error("Multiple instances of Game are not allowed");
            failed_ = true;
            return;
        }

        instance = this;
        owns_instance_ = true;

        if (const auto window_result = initialize_window(); !window_result)
        {
            Log::error(window_result.error());
            failed_ = true;
            return;
        }

        if (const auto graphics_result = initialize_graphics(); !graphics_result)
        {
            Log::error(graphics_result.error());
            failed_ = true;
            return;
        }

        if (const auto ui_result = terrain_tools_.initialize_ui_assets(); !ui_result)
        {
            Log::error(ui_result.error());
            failed_ = true;
            return;
        }

        if (const auto hud_result = inventory_hud_.initialize_assets(); !hud_result)
        {
            Log::error(hud_result.error());
            failed_ = true;
            return;
        }

        if (const auto world_renderer_result = world_renderer_.initialize_assets(); !world_renderer_result)
        {
            Log::error(world_renderer_result.error());
            failed_ = true;
            return;
        }

        if (!win_font_.openFromFile("assets/fonts/Cinzel-SemiBold.ttf"))
        {
            Log::error("Failed to load font 'assets/fonts/Cinzel-SemiBold.ttf'");
            failed_ = true;
            return;
        }

        if (const auto world_result = initialize_world_state(); !world_result)
        {
            Log::error(world_result.error());
            failed_ = true;
        }
    }

    Game::~Game()
    {
        if (owns_instance_) instance = nullptr;
        destroy_world();
        destroy_graphics();
    }

    void Game::run()
    {
        if (failed_) return;

        while (window_.isOpen())
        {
            input_.begin_frame();
            const auto [should_close, resized, new_size] = input_.update(window_);

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

    void Game::quit()
    {
        if (instance == nullptr)
        {
            Log::warn("Game::quit() ignored because no active game instance exists");
            return;
        }

        instance->window_.close();
    }

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
                .depthBits = 24,
                .stencilBits = 8,
                .majorVersion = 4,
                .minorVersion = 6,
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

        camera_settings_.world_span = { world_state_.terrain().chunk_size().x * 1.75f, world_state_.terrain().chunk_size().y * 1.75f };

        update_world_view(settings_.win_size);

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
        world_renderer_.destroy_graphics_resources();
        terrain_tools_.destroy_graphics_resources();
        win_font_ = sf::Font{};
        gfx::Shader::clear_cache();
        gladLoaderUnloadGL();
        gl_loaded_ = false;
    }

    void Game::render_opengl()
    {
        // Draw the GL world first; the SFML pass that follows resets shared GL state.
        if (const auto context = terrain_tool_context(); context.has_value())
        {
            world_renderer_.draw(world_state_, terrain_tools_.active_water_preview(*context), world_view_);
            return;
        }

        world_renderer_.draw(world_state_, std::nullopt, world_view_);
    }

    void Game::render_sfml()
    {
        window_.setView(world_view_);
        // The player and aim overlay stay on the SFML path so they can draw cleanly on top of the GL world.
        if (const auto context = terrain_tool_context(); context.has_value()) terrain_tools_.draw_targeting_debug_overlay(window_, *context);

        if (world_state_.ready()) world_state_.player().draw_sf(window_);

        window_.setView(make_ui_view());
        if (world_state_.ready()) inventory_hud_.draw(window_, world_state_.resources().hud_state());
        terrain_tools_.draw_ui(window_);
        draw_goal_progress_bar();
        if (player_won_) draw_win_overlay();
    }

    void Game::draw_goal_progress_bar()
    {
        if (!world_state_.ready()) return;

        const auto target_size = window_.getSize();
        const float ui_scale = std::clamp(static_cast<float>(target_size.x) / 800.0f, 0.72f, 1.18f);
        const sf::Vector2f bar_size{std::max(80.0f, std::min(static_cast<float>(target_size.x) - 48.0f, 420.0f * ui_scale)),
                                    18.0f * ui_scale};
        const sf::Vector2f bar_position{(static_cast<float>(target_size.x) - bar_size.x) * 0.5f, 18.0f * ui_scale};
        const float progress = std::clamp(last_green_surface_coverage_ / required_green_surface_coverage_, 0.0f, 1.0f);

        sf::RectangleShape shadow{{bar_size.x + 8.0f * ui_scale, bar_size.y + 8.0f * ui_scale}};
        shadow.setPosition({bar_position.x - 4.0f * ui_scale, bar_position.y - 4.0f * ui_scale});
        shadow.setFillColor(0x030604A8_rgba);
        window_.draw(shadow);

        sf::RectangleShape background{bar_size};
        background.setPosition(bar_position);
        background.setFillColor(0x172014E6_rgba);
        background.setOutlineColor(0xD7F2C9D8_rgba);
        background.setOutlineThickness(std::max(1.0f, 1.5f * ui_scale));
        window_.draw(background);

        const float inset = std::max(2.0f, 3.0f * ui_scale);
        const sf::Vector2f fill_size{std::max(0.0f, (bar_size.x - inset * 2.0f) * progress), std::max(1.0f, bar_size.y - inset * 2.0f)};
        sf::RectangleShape fill{fill_size};
        fill.setPosition({bar_position.x + inset, bar_position.y + inset});
        fill.setFillColor(0x68E85FFF_rgba);
        window_.draw(fill);
    }

    void Game::draw_win_overlay()
    {
        const auto target_size = window_.getSize();
        const sf::Vector2f center{static_cast<float>(target_size.x) * 0.5f, static_cast<float>(target_size.y) * 0.5f};

        sf::RectangleShape dim{{static_cast<float>(target_size.x), static_cast<float>(target_size.y)}};
        dim.setFillColor(0x07120CBC_rgba);
        window_.draw(dim);

        sf::Text title{win_font_, "PLANET RESTORED", 54u};
        title.setFillColor(0x9CFF7CFF_rgba);
        title.setOutlineColor(0x061006E6_rgba);
        title.setOutlineThickness(2.4f);
        const auto title_bounds = title.getLocalBounds();
        title.setOrigin({title_bounds.position.x + title_bounds.size.x * 0.5f, title_bounds.position.y + title_bounds.size.y * 0.5f});
        title.setPosition({center.x, center.y - 24.0f});
        window_.draw(title);

        sf::Text subtitle{win_font_, "The planet is green again", 22u};
        subtitle.setFillColor(0xECFFE7FF_rgba);
        subtitle.setOutlineColor(0x061006D0_rgba);
        subtitle.setOutlineThickness(1.4f);
        const auto subtitle_bounds = subtitle.getLocalBounds();
        subtitle.setOrigin({subtitle_bounds.position.x + subtitle_bounds.size.x * 0.5f, subtitle_bounds.position.y + subtitle_bounds.size.y * 0.5f});
        subtitle.setPosition({center.x, center.y + 38.0f});
        window_.draw(subtitle);
    }

    Result<void> Game::create_world()
    {
        b2WorldDef world_def = b2DefaultWorldDef();
        world_def.gravity = { .x = 0.0f, .y = 0.0f };
        world_ = b2CreateWorld(&world_def);
        if (!b2World_IsValid(world_)) return fail("Failed to create the Box2D world");
        return {};
    }

    void Game::handle_frame_input()
    {
        if (player_won_)
            return;

        handle_scroll_input();
        if (handle_global_shortcuts())
            return;

        if (terrain_tools_.upgrade_menu_open())
        {
            handle_modal_input();
            return;
        }

        handle_gameplay_input();
    }

    void Game::handle_scroll_input()
    {
        if (const float scroll_delta = input_.mouse_wheel_delta(); scroll_delta != 0.0f)
        {
            // Plain scroll swaps tools; Ctrl + scroll is reserved for camera zoom.
            if (input_.is_pressed(Key::LControl) || input_.is_pressed(Key::RControl))
            {
                constexpr float zoom_step = 0.12f;
                camera_state_.zoom = std::clamp(
                    camera_state_.zoom * (1.0f - scroll_delta * zoom_step), camera_settings_.min_zoom, camera_settings_.max_zoom);
                update_world_view(window_.getSize());
            }
            else terrain_tools_.handle_scroll(scroll_delta);
        }
    }

    bool Game::handle_global_shortcuts()
    {
#ifndef NDEBUG
        if (input_.just_pressed(Key::P)) export_current_chunk_field();
#endif

        if (input_.just_pressed(Key::E))
        {
            terrain_tools_.toggle_upgrade_menu();
            return true;
        }

#ifndef NDEBUG
        if (input_.just_pressed(Key::Num0))
        {
            if (const auto context = terrain_tool_context(); context.has_value())
                terrain_tools_.handle_zero_shortcut(*context);
        }

        if (input_.just_pressed(Key::Num9))
        {
            if (world_state_.ready())
            {
                world_state_.player().teleport(world_state_.initial_spawn_position(), world_state_.terrain().planet_center());
                physics_accumulator_ = 0.0f;
                camera_state_.initialized = false;
            }
        }
#endif

        if (input_.just_pressed(Key::Escape))
        {
            terrain_tools_.close_upgrade_menu();
            terrain_tools_.cancel_active_interaction();
            return true;
        }

        return false;
    }

    void Game::handle_modal_input()
    {
        if (input_.just_pressed(MouseButton::Left))
        {
            if (const auto context = terrain_tool_context(); context.has_value())
            {
                const auto pixel_position = sf::Mouse::getPosition(window_);
                const auto ui_position = window_.mapPixelToCoords(pixel_position, make_ui_view());
                terrain_tools_.handle_upgrade_menu_click(*context, ui_position, window_.getSize());
            }
        }
    }

    void Game::handle_gameplay_input()
    {
        if (input_.just_pressed(MouseButton::Left))
            handle_tool_mouse_pressed(MouseButton::Left);
        if (input_.just_pressed(MouseButton::Right))
            handle_tool_mouse_pressed(MouseButton::Right);
    }

    void Game::fixed_update(const float dt)
    {
        if (!b2World_IsValid(world_) || !world_state_.ready()) return;
        if (player_won_) return;
        world_state_.water().update_active_colliders(world_state_.terrain(), world_state_.player().world_position());

        physics_accumulator_ = std::min(physics_accumulator_ + dt, 0.25f);

        static constexpr float fixed_step = 1.0f / 60.0f;
        static constexpr int sub_steps = 4;
        auto& player = world_state_.player();
        const auto& terrain = world_state_.terrain();
        const vec2 planet_center = terrain.planet_center();

        auto refresh_player_state = [&player, &terrain, planet_center]
        {
            // Sample contacts around each physics step so jump and swim state stays in sync with terrain edits.
            player.refresh_grounded_state(planet_center);
            player.set_in_water(player.is_in_water() || terrain.contains_water_volume(player.world_position()));
        };

        refresh_player_state();

        while (physics_accumulator_ >= fixed_step)
        {
            const bool in_water = player.is_in_water();
            player.prepare_for_physics_step(fixed_step, planet_center, in_water, input_);
            b2World_Step(world_, fixed_step, sub_steps);
            refresh_player_state();
            physics_accumulator_ -= fixed_step;
        }

        player.sync_from_physics(planet_center);
        player.set_in_water(player.is_in_water() || terrain.contains_water_volume(player.world_position()));
    }

    void Game::variable_update(const float dt)
    {
        if (!player_won_)
            update_terrain_editing(dt);
        world_state_.update(dt);
        update_win_condition();
        sync_camera_to_player(dt);
    }

    void Game::update_win_condition()
    {
        if (player_won_ || !world_state_.ready())
            return;

        auto& terrain = world_state_.terrain();
        const std::uint64_t field_revision = terrain.field_revision();
        if (field_revision == last_win_check_field_revision_)
            return;

        last_win_check_field_revision_ = field_revision;
        last_green_surface_coverage_ = terrain.green_surface_coverage();
        if (last_green_surface_coverage_ < required_green_surface_coverage_)
            return;

        player_won_ = true;
        terrain_tools_.close_upgrade_menu();
        terrain_tools_.cancel_active_interaction();
        Log::info("Planet restored: {:.0f}% of the surface is green", last_green_surface_coverage_ * 100.0f);
    }

    void Game::sync_camera_to_player(const float dt)
    {
        if (!world_state_.ready()) return;

        const vec2 player_position = world_state_.player().world_position();
        if (!camera_state_.initialized)
        {
            camera_state_.focus_world = player_position;
            camera_state_.rotation_radians = dir_to_angle(player_up_dir());
            camera_state_.initialized = true;
        }

        vec2 desired_focus = camera_state_.focus_world;
        const vec2 player_delta = player_position - camera_state_.focus_world;
        const float follow_threshold_sq = camera_settings_.follow_threshold * camera_settings_.follow_threshold;
        const bool move_input_active = is_player_move_input_active();

        if (move_input_active)
        {
            // Give the player a small dead zone before the camera starts chasing them.
            const float distance_sq = player_delta.lengthSquared();
            if (distance_sq > follow_threshold_sq)
            {
                desired_focus = player_position - normalize(player_delta, { 1.0f, 0.0f }) * camera_settings_.follow_threshold;
            }
        }
        else desired_focus = player_position;

        const float position_alpha =
            smooth_factor(move_input_active ? camera_settings_.follow_smoothing : camera_settings_.recenter_smoothing, dt);
        camera_state_.focus_world = lerp(camera_state_.focus_world, desired_focus, position_alpha);

        const vec2 planet_center = world_state_.terrain().planet_center();
        // Rotate the camera from the planet center instead of the player body so jumps do not make it wobble.
        const vec2 camera_up_dir = normalize(camera_state_.focus_world - planet_center, player_up_dir());
        const float target_rotation = dir_to_angle(camera_up_dir);
        camera_state_.rotation_radians +=
            shortest_angle_delta(camera_state_.rotation_radians, target_rotation) * smooth_factor(camera_settings_.rotation_smoothing, dt);

        world_view_.setCenter(camera_state_.focus_world);
        world_view_.setRotation(sf::radians(camera_state_.rotation_radians));
        window_.setView(world_view_);
    }

    void Game::update_terrain_editing(const float dt)
    {
        if (const auto context = terrain_tool_context(); context.has_value()) terrain_tools_.update(*context, dt);
    }

    void Game::export_current_chunk_field()
    {
        if (const auto context = terrain_tool_context(); context.has_value()) terrain_tools_.export_current_chunk_field(*context);
    }

    void Game::handle_tool_mouse_pressed(const MouseButton button)
    {
        if (const auto context = terrain_tool_context(); context.has_value()) terrain_tools_.handle_mouse_pressed(*context, button);
    }

    void Game::handle_resize(const uvec2 size)
    {
        apply_viewport(size);
        update_world_view(size);
    }

    void Game::apply_viewport(const uvec2 size) const
    {
        if (size.x == 0 || size.y == 0) return;

        glViewport(0, 0, static_cast<std::int32_t>(size.x), static_cast<std::int32_t>(size.y));
    }

    std::optional<tools::TerrainToolContext> Game::terrain_tool_context()
    {
        if (!b2World_IsValid(world_) || !world_state_.ready()) return std::nullopt;

        return tools::TerrainToolContext{
            .world = world_,
            .terrain = &world_state_.terrain(),
            .resources = &world_state_.resources(),
            .water = &world_state_.water(),
            .input = &input_,
            .player_body = world_state_.player().body(),
            .player_world_position = world_state_.player().world_position(),
            .mouse_world_position = mouse_world_position()
        };
    }

    vec2 Game::mouse_world_position() const
    {
        const auto pixel_position = sf::Mouse::getPosition(window_);
        const auto world_position = window_.mapPixelToCoords(pixel_position, world_view_);
        return { world_position.x, world_position.y };
    }

    sf::View Game::make_ui_view() const
    {
        const auto window_size = window_.getSize();
        return {
            { static_cast<float>(window_size.x) * 0.5f, static_cast<float>(window_size.y) * 0.5f },
            { static_cast<float>(window_size.x), static_cast<float>(window_size.y) }
        };
    }

    vec2 Game::player_up_dir() const
    {
        if (!world_state_.ready()) return { 0.0f, 1.0f };
        return world_state_.player().up_direction(world_state_.terrain().planet_center());
    }

    bool Game::is_player_move_input_active() const
    {
        return world_state_.ready() && world_state_.player().is_move_input_active(input_);
    }

    void Game::update_world_view(const uvec2 size)
    {
        if (size.x == 0 || size.y == 0) return;

        const float window_aspect = static_cast<float>(size.x) / static_cast<float>(size.y);

        float view_width = std::max(camera_settings_.world_span.x * camera_state_.zoom, 0.001f);
        float view_height = std::max(camera_settings_.world_span.y * camera_state_.zoom, 0.001f);

        if (view_width / view_height > window_aspect) view_height = view_width / window_aspect;
        else view_width = view_height * window_aspect;

        world_view_.setSize({ view_width, -view_height });
        sync_camera_to_player(0.0f);
    }
}
