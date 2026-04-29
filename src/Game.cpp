#include "pch.hpp"
#include "Game.hpp"

#include "Input.hpp"

namespace game
{
	namespace
	{
		Game* instance{ nullptr };
	}


	Game::Game(GameSettings settings) : settings_{ std::move(settings) }
	{
		assert(!instance && "Multiple instances of Game are not allowed");
		instance = this;

		initialize_window();
		if (!initialize_graphics())
		{
			failed_ = true;
			return;
		}

		terrain_tools_.initialize_ui_assets();
		configure_input();

		try
		{
			initialize_world_state();
		}
		catch (const std::exception& exception)
		{
			std::println(std::cerr, "Initialization failed: {}", exception.what());
			failed_ = true;
		}
	}

	Game::~Game()
	{
		instance = nullptr;
		destroy_world();
		destroy_graphics();
	}


	void Game::run()
	{
		if (failed_) return;
		assert(instance == this && "Game instance is not properly initialized");

		while (window_.isOpen())
		{
			const auto [
				should_close,
				resized,
				new_size
			] = Input::instance().update(window_);

			if (should_close) break;
			if (resized) handle_resize(new_size);

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
		assert(instance && "No active Game instance to quit");
		instance->window_.close();
	}


	void Game::update(const float dt)
	{
		step_physics(dt);
		sync_camera_to_player(dt);
		if (terrain_) terrain_->update(dt);
		update_terrain_editing(dt);
	}

	void Game::initialize_window()
	{
		window_ = sf::RenderWindow
		{
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
	}

	bool Game::initialize_graphics()
	{
		if (!window_.setActive(true))
		{
			std::println(std::cerr, "Failed to activate OpenGL context");
			return false;
		}

		if (gladLoaderLoadGL() == 0)
		{
			std::println(std::cerr, "Failed to initialize GLAD");
			return false;
		}

		gl_loaded_ = true;
		apply_viewport(settings_.win_size);
		return true;
	}

	void Game::initialize_world_state()
	{
		create_world();
		terrain_.emplace(world_);
		camera_settings_.world_span = {
			terrain_->chunk_size().x * 1.75f,
			terrain_->chunk_size().y * 1.75f
		};
		create_player();
		update_world_view(settings_.win_size);
	}

	void Game::destroy_world()
	{
		terrain_.reset();
		player_.destroy();

		if (!b2World_IsValid(world_)) return;

		b2DestroyWorld(world_);
		world_ = b2_nullWorldId;
	}

	void Game::destroy_graphics()
	{
		if (!gl_loaded_) return;

		gfx::Shader::clear_cache();
		gladLoaderUnloadGL();
		gl_loaded_ = false;
	}

	void Game::render_opengl()
	{
		if (terrain_) terrain_->draw_gl(world_view_);
		if (terrain_) terrain_->draw_water_gl(world_view_);
		if (const auto context = terrain_tool_context(); context.has_value())
		{
			terrain_tools_.draw_world_preview(*context, world_view_);
		}
	}

	void Game::render_sfml()
	{
		window_.setView(world_view_);

		if (terrain_) terrain_->draw_overlays(window_, world_view_);
		player_.draw_sf(window_);

		window_.setView(make_ui_view());
		if (terrain_) terrain_->draw_resource_ui(window_);
		terrain_tools_.draw_ui(window_);
	}

	void Game::create_world()
	{
		b2WorldDef world_def = b2DefaultWorldDef();
		world_def.gravity    = { .x = 0.0f, .y = 0.0f };
		world_               = b2CreateWorld(&world_def);
	}

	void Game::create_player()
	{
		assert(terrain_ && "Terrain must exist before creating the player");

		const auto spawn = terrain_->spawn_point_from_top_center(
			player_config_.capsule_half_height + player_config_.spawn_air_clearance);
		player_.create(world_, spawn, terrain_->planet_center(), player_config_);
		camera_state_.initialized = false;
	}

	void Game::configure_input()
	{
		Input::on([this](const Event::MouseWheelScrolled& scroll)
		{
			if (Input::is_pressed(Key::LControl) || Input::is_pressed(Key::RControl))
			{
				constexpr float zoom_step = 0.12f;
				camera_state_.zoom = std::clamp(
					camera_state_.zoom * (1.0f - scroll.delta * zoom_step),
					camera_settings_.min_zoom,
					camera_settings_.max_zoom);
				update_world_view(window_.getSize());
				return;
			}

			terrain_tools_.handle_scroll(scroll.delta);
		});

		Input::on<Event::KeyPressed>(Key::P, [this]
		{
			export_current_chunk_field();
		});

		Input::on<Event::KeyPressed>(Key::Enter, [this]
		{
			terrain_tools_.handle_upgrade();
		});

		Input::on<Event::KeyPressed>(Key::Num0, [this]
		{
			terrain_tools_.handle_zero_shortcut();
		});

		Input::on<Event::KeyPressed>(Key::Escape, [this]
		{
			terrain_tools_.cancel_bucket_placement();
		});

		Input::on<Event::MouseButtonPressed>(MouseButton::Left, [this]
		{
			handle_tool_mouse_pressed(MouseButton::Left);
		});

		Input::on<Event::MouseButtonPressed>(MouseButton::Right, [this]
		{
			handle_tool_mouse_pressed(MouseButton::Right);
		});
	}

	void Game::step_physics(const float dt)
	{
		if (!b2World_IsValid(world_)) return;

		physics_accumulator_ = std::min(physics_accumulator_ + dt, 0.25f);

		static constexpr float fixed_step = 1.0f / 60.0f;
		static constexpr int   sub_steps  = 4;
		const vec2             planet_center = terrain_ ? terrain_->planet_center() : vec2{ 0.0f, 0.0f };

		player_.refresh_grounded_state(planet_center);

		while (physics_accumulator_ >= fixed_step)
		{
			player_.prepare_for_physics_step(fixed_step, planet_center);
			b2World_Step(world_, fixed_step, sub_steps);
			player_.refresh_grounded_state(planet_center);
			physics_accumulator_ -= fixed_step;
		}

		player_.sync_from_physics(planet_center);
	}

	void Game::sync_camera_to_player(const float dt)
	{
		if (!player_.valid()) return;

		const vec2 player_position = player_.world_position();
		if (!camera_state_.initialized)
		{
			camera_state_.focus_world = player_position;
			camera_state_.rotation_radians = angle_from_up_direction(player_up_direction());
			camera_state_.initialized = true;
		}

		vec2        desired_focus       = camera_state_.focus_world;
		const vec2  player_delta        = subtract_vec2(player_position, camera_state_.focus_world);
		const float follow_threshold_sq = camera_settings_.follow_threshold * camera_settings_.follow_threshold;
		const bool  move_input_active   = is_player_move_input_active();

		if (move_input_active)
		{
			const float distance_sq = player_delta.lengthSquared();
			if (distance_sq > follow_threshold_sq)
			{
				desired_focus = subtract_vec2(
					player_position,
					scale_vec2(normalize_vec2(player_delta, { 1.0f, 0.0f }), camera_settings_.follow_threshold));
			}
		}
		else
		{
			desired_focus = player_position;
		}

		const float position_alpha = smooth_factor(
			move_input_active ? camera_settings_.follow_smoothing : camera_settings_.recenter_smoothing,
			dt);
		camera_state_.focus_world = lerp_vec2(camera_state_.focus_world, desired_focus, position_alpha);

		const vec2 planet_center       = terrain_ ? terrain_->planet_center() : vec2{ 0.0f, 0.0f };
		const vec2 camera_up_direction = normalize_vec2(
			subtract_vec2(camera_state_.focus_world, planet_center),
			player_up_direction());
		const float target_rotation = angle_from_up_direction(camera_up_direction);
		camera_state_.rotation_radians += shortest_angle_delta(camera_state_.rotation_radians, target_rotation) *
			smooth_factor(camera_settings_.rotation_smoothing, dt);

		world_view_.setCenter(camera_state_.focus_world);
		world_view_.setRotation(sf::radians(camera_state_.rotation_radians));
		window_.setView(world_view_);
	}

	void Game::update_terrain_editing(const float dt)
	{
		if (const auto context = terrain_tool_context(); context.has_value())
		{
			terrain_tools_.update(*context, dt);
		}
	}

	void Game::export_current_chunk_field()
	{
		if (const auto context = terrain_tool_context(); context.has_value())
		{
			terrain_tools_.export_current_chunk_field(*context);
		}
	}

	void Game::handle_tool_mouse_pressed(const MouseButton button)
	{
		if (const auto context = terrain_tool_context(); context.has_value())
		{
			terrain_tools_.handle_mouse_pressed(*context, button);
		}
	}

	void Game::handle_resize(const uvec2 size)
	{
		apply_viewport(size);
		update_world_view(size);
	}

	void Game::apply_viewport(const uvec2 size) const
	{
		if (size.x == 0 || size.y == 0) return;

		glViewport(
			0,
			0,
			static_cast<std::int32_t>(size.x),
			static_cast<std::int32_t>(size.y));
	}

	std::optional<tools::TerrainToolContext> Game::terrain_tool_context()
	{
		if (!terrain_ || !b2World_IsValid(world_) || !player_.valid()) return std::nullopt;

		return tools::TerrainToolContext{
			.world = world_,
			.terrain = &*terrain_,
			.player_body = player_.body(),
			.player_world_position = player_.world_position(),
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
			{
				static_cast<float>(window_size.x) * 0.5f,
				static_cast<float>(window_size.y) * 0.5f
			},
			{
				static_cast<float>(window_size.x),
				static_cast<float>(window_size.y)
			}
		};
	}


	vec2 Game::player_up_direction() const
	{
		const vec2 planet_center = terrain_ ? terrain_->planet_center() : vec2{ 0.0f, 0.0f };
		return player_.up_direction(planet_center);
	}

	bool Game::is_player_move_input_active() const
	{
		return player_.is_move_input_active();
	}

	void Game::update_world_view(const uvec2 size)
	{
		if (size.x == 0 || size.y == 0) return;

		const float window_aspect = static_cast<float>(size.x) / static_cast<float>(size.y);

		float view_width  = std::max(camera_settings_.world_span.x * camera_state_.zoom, 0.001f);
		float view_height = std::max(camera_settings_.world_span.y * camera_state_.zoom, 0.001f);

		if (view_width / view_height > window_aspect) view_height = view_width / window_aspect;
		else view_width                                           = view_height * window_aspect;

		world_view_.setSize({ view_width, -view_height });
		sync_camera_to_player(0.0f);
	}
}
