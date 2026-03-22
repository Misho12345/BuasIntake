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

		window_.setFramerateLimit(60);
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
		terrain_.reset();

		if (b2Body_IsValid(player_.body))
		{
			b2DestroyBody(player_.body);
			player_.body = b2_nullBodyId;
		}

		if (b2World_IsValid(world_))
		{
			b2DestroyWorld(world_);
			world_ = b2_nullWorldId;
		}

		instance = nullptr;

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
		sync_camera_to_player();
	}

	void Game::render_opengl() const
	{
		if (terrain_) terrain_->draw_gl(world_view_);
		player_.draw_gl(world_view_);
	}

	void Game::render_sfml()
	{
		window_.setView(world_view_);

		if (terrain_) terrain_->render_debug(window_);
		player_.draw_sf(window_);
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

		const auto spawn = terrain_->spawn_point_from_top_center(10.0f);

		b2BodyDef body_def         = b2DefaultBodyDef();
		body_def.type              = b2_dynamicBody;
		body_def.position          = { .x = spawn.x, .y = spawn.y };
		body_def.linearDamping     = 2.5f;
		body_def.angularDamping    = 0.8f;
		body_def.allowFastRotation = true;
		body_def.name              = "player_ball";

		player_      = GameObject::make_sf<sf::CircleShape>(player_radius_, 40);
		player_.body = b2CreateBody(world_, &body_def);

		b2ShapeDef shape_def           = b2DefaultShapeDef();
		shape_def.density              = 1.1f;
		shape_def.material.friction    = 0.8f;
		shape_def.material.restitution = 0.1f;

		const b2Circle circle{ { 0.0f, 0.0f }, player_radius_ };
		b2CreateCircleShape(player_.body, &shape_def, &circle);

		auto& circle_shape = dynamic_cast<sf::CircleShape&>(*std::get<GameObject::SfDrawable>(player_.renderable));
		circle_shape.setOrigin({ player_radius_, player_radius_ });
		circle_shape.setFillColor(0xF29E4C_rgb);
		circle_shape.setOutlineColor(0xFFF3D9_rgb);
		circle_shape.setOutlineThickness(0.08f);

		player_.sync_from_physics();
		terrain_->update_active_colliders(player_.transformable.getPosition());
	}

	void Game::apply_player_input() const
	{
		if (!b2Body_IsValid(player_.body)) return;

		b2Vec2          force{ 0.0f, 0.0f };
		constexpr float force_strength = 35.0f;

		if (Input::is_pressed(Key::Left)) force.x -= force_strength;
		if (Input::is_pressed(Key::Right)) force.x += force_strength;
		if (Input::is_pressed(Key::Up)) force.y += force_strength;
		if (Input::is_pressed(Key::Down)) force.y -= force_strength;

		if (force.x != 0.0f || force.y != 0.0f)
		{
			b2Body_ApplyForceToCenter(player_.body, force, true);
		}
	}

	void Game::configure_input()
	{
		Input::on([](const Event::MouseWheelScrolled& scroll)
		{
			if (!instance) return;

			constexpr float zoom_step = 0.12f;
			instance->camera_zoom_ = std::clamp(
				instance->camera_zoom_ * (1.0f - scroll.delta * zoom_step),
				instance->min_camera_zoom_,
				instance->max_camera_zoom_);
			instance->update_world_view(instance->window_.getSize());
		});
	}

	void Game::step_physics(const float dt)
	{
		if (!b2World_IsValid(world_)) return;

		physics_accumulator_ = std::min(physics_accumulator_ + dt, 0.25f);

		static constexpr float fixed_step = 1.0f / 60.0f;
		static constexpr int   sub_steps  = 4;

		while (physics_accumulator_ >= fixed_step)
		{
			apply_player_input();
			terrain_->update_active_colliders(player_.transformable.getPosition());
			b2World_Step(world_, fixed_step, sub_steps);
			physics_accumulator_ -= fixed_step;
		}

		player_.sync_from_physics();
		terrain_->update_active_colliders(player_.transformable.getPosition());
	}

	void Game::sync_camera_to_player()
	{
		if (!b2Body_IsValid(player_.body)) return;

		world_view_.setCenter(player_.transformable.getPosition());
		window_.setView(world_view_);
	}

	void Game::update_world_view(const uvec2 size)
	{
		if (size.x == 0 || size.y == 0) return;

		const float window_aspect = static_cast<float>(size.x) / static_cast<float>(size.y);

		float view_width  = std::max(camera_world_span_.x * camera_zoom_, 0.001f);
		float view_height = std::max(camera_world_span_.y * camera_zoom_, 0.001f);

		if (view_width / view_height > window_aspect) view_height = view_width / window_aspect;
		else view_width = view_height * window_aspect;

		world_view_.setSize({ view_width, -view_height });
		sync_camera_to_player();
	}
}
