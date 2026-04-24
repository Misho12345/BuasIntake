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

		if (!window_.setActive(true))
		{
			std::println(std::cerr, "Failed to activate OpenGL context");
			failed_ = true;
			return;
		}

		if (gladLoaderLoadGL() == 0)
		{
			std::println(std::cerr, "Failed to initialize GLAD");
			failed_ = true;
			return;
		}

		gl_loaded_ = true;
		glViewport(0, 0, static_cast<std::int32_t>(settings_.win_size.x),
		           static_cast<std::int32_t>(settings_.win_size.y));
		terrain_tools_.initialize_ui_assets();
		configure_input();

		try
		{
			create_world();
			terrain_.emplace(world_);
			camera_world_span_ = {
				terrain_->chunk_size().x * 1.75f,
				terrain_->chunk_size().y * 1.75f
			};
			create_player();
			update_world_view(settings_.win_size);
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
		terrain_.reset();
		player_.destroy();

		if (b2World_IsValid(world_))
		{
			b2DestroyWorld(world_);
			world_ = b2_nullWorldId;
		}

		if (gl_loaded_)
		{
			gfx::Shader::clear_cache();
			gladLoaderUnloadGL();
			gl_loaded_ = false;
		}
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
			update(clock_.restart().asSeconds());

			window_.clear(settings_.clear_color);

			if (resized)
			{
				glViewport(
					0, 0,
					static_cast<std::int32_t>(new_size.x),
					static_cast<std::int32_t>(new_size.y));
				update_world_view(new_size);
			}

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
		update_terrain_editing(dt);
	}

	void Game::render_opengl() const
	{
		if (terrain_) terrain_->draw_gl(world_view_);
		if (terrain_) terrain_->draw_water_gl(world_view_);
		player_.draw_gl(world_view_);
		if (const auto context = const_cast<Game*>(this)->terrain_tool_context(); context.has_value())
		{
			terrain_tools_.draw_world_preview(*context, world_view_);
		}
	}

	void Game::render_sfml()
	{
		window_.setView(world_view_);

		//if (terrain_) terrain_->render_debug(window_);
		player_.draw_sf(window_);

		const auto window_size = window_.getSize();
		const sf::View ui_view{
			{
				static_cast<float>(window_size.x) * 0.5f,
				static_cast<float>(window_size.y) * 0.5f
			},
			{
				static_cast<float>(window_size.x),
				static_cast<float>(window_size.y)
			}
		};
		window_.setView(ui_view);
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
		camera_initialized_   = false;
		terrain_->update_active_colliders(player_.world_position());
	}

	void Game::configure_input()
	{
		Input::on([this](const Event::MouseWheelScrolled& scroll)
		{
			if (Input::is_pressed(Key::LControl) || Input::is_pressed(Key::RControl))
			{
				constexpr float zoom_step = 0.12f;
				camera_zoom_ = std::clamp(
					camera_zoom_ * (1.0f - scroll.delta * zoom_step),
					min_camera_zoom_,
					max_camera_zoom_);
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
			terrain_tools_.refill_bucket();
		});

		Input::on<Event::KeyPressed>(Key::Escape, [this]
		{
			terrain_tools_.cancel_bucket_placement();
		});

		Input::on<Event::MouseButtonPressed>(MouseButton::Left, [this]
		{
			handle_water_input(MouseButton::Left);
		});

		Input::on<Event::MouseButtonPressed>(MouseButton::Right, [this]
		{
			handle_water_input(MouseButton::Right);
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
			if (terrain_) terrain_->update_active_colliders(player_.world_position());
			b2World_Step(world_, fixed_step, sub_steps);
			player_.refresh_grounded_state(planet_center);
			physics_accumulator_ -= fixed_step;
		}

		player_.sync_from_physics(planet_center);
		if (terrain_) terrain_->update_active_colliders(player_.world_position());
	}

	void Game::sync_camera_to_player(const float dt)
	{
		if (!player_.valid()) return;

		const vec2 player_position = player_.world_position();
		if (!camera_initialized_)
		{
			camera_focus_world_      = player_position;
			camera_rotation_radians_ = angle_from_up_direction(player_up_direction());
			camera_initialized_      = true;
		}

		vec2        desired_focus       = camera_focus_world_;
		const vec2  player_delta        = subtract_vec2(player_position, camera_focus_world_);
		const float follow_threshold_sq = camera_follow_threshold_ * camera_follow_threshold_;
		const bool  move_input_active   = is_player_move_input_active();

		if (move_input_active)
		{
			const float distance_sq = player_delta.lengthSquared();
			if (distance_sq > follow_threshold_sq)
			{
				desired_focus = subtract_vec2(
					player_position,
					scale_vec2(normalize_vec2(player_delta, { 1.0f, 0.0f }), camera_follow_threshold_));
			}
		}
		else
		{
			desired_focus = player_position;
		}

		const float position_alpha = smooth_factor(
			move_input_active ? camera_follow_smoothing_ : camera_recenter_smoothing_,
			dt);
		camera_focus_world_ = lerp_vec2(camera_focus_world_, desired_focus, position_alpha);

		const vec2 planet_center       = terrain_ ? terrain_->planet_center() : vec2{ 0.0f, 0.0f };
		const vec2 camera_up_direction = normalize_vec2(
			subtract_vec2(camera_focus_world_, planet_center),
			player_up_direction());
		const float target_rotation = angle_from_up_direction(camera_up_direction);
		camera_rotation_radians_    += shortest_angle_delta(camera_rotation_radians_, target_rotation) *
				smooth_factor(camera_rotation_smoothing_, dt);

		world_view_.setCenter(camera_focus_world_);
		world_view_.setRotation(sf::radians(camera_rotation_radians_));
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

	void Game::handle_water_input(const MouseButton button)
	{
		if (const auto context = terrain_tool_context(); context.has_value())
		{
			terrain_tools_.handle_mouse_pressed(*context, button);
		}
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

		float view_width  = std::max(camera_world_span_.x * camera_zoom_, 0.001f);
		float view_height = std::max(camera_world_span_.y * camera_zoom_, 0.001f);

		if (view_width / view_height > window_aspect) view_height = view_width / window_aspect;
		else view_width                                           = view_height * window_aspect;

		world_view_.setSize({ view_width, -view_height });
		sync_camera_to_player(0.0f);
	}
}
