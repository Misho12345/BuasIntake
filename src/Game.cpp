#include "pch.hpp"
#include "Game.hpp"

#include "Input.hpp"

namespace game
{
	namespace
	{
		Game* instance{ nullptr };

		mat4 make_projection(const sf::View& view)
		{
			const auto center = view.getCenter();
			const auto size   = view.getSize();

			const float left   = center.x - size.x * 0.5f;
			const float right  = center.x + size.x * 0.5f;
			const float bottom = center.y + size.y * 0.5f;
			const float top    = center.y - size.y * 0.5f;

			const float inv_width  = 1.0f / std::max(right - left, 0.001f);
			const float inv_height = 1.0f / std::max(top - bottom, 0.001f);

			const std::array proj
			{
				2.0f * inv_width, 0.0f, 0.0f, 0.0f,
				0.0f, 2.0f * inv_height, 0.0f, 0.0f,
				0.0f, 0.0f, -1.0f, 0.0f,
				-(right + left) * inv_width, -(top + bottom) * inv_height, 0.0f, 1.0f
			};

			return mat4{ proj.data() };
		}
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

		try
		{
			terrain_renderer_.emplace();
			create_world();
			terrain_.emplace(world_);
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
		terrain_renderer_.reset();

		if (b2Body_IsValid(player_body_))
		{
			b2DestroyBody(player_body_);
			player_body_ = b2_nullBodyId;
		}

		if (b2World_IsValid(world_))
		{
			b2DestroyWorld(world_);
			world_ = b2_nullWorldId;
		}

		instance = nullptr;

		if (gl_loaded_)
		{
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
	}

	void Game::render_opengl() const
	{
		if (!terrain_ || !terrain_renderer_) return;

		terrain_renderer_->draw(terrain_->mesh(), make_projection(world_view_));
	}

	void Game::render_sfml()
	{
		window_.setView(world_view_);

		if (terrain_) terrain_->render_debug(window_);
		if (b2Body_IsValid(player_body_)) window_.draw(player_shape_);
	}

	void Game::create_world()
	{
		b2WorldDef world_def = b2DefaultWorldDef();
		world_def.gravity    = { 0.0f, -18.0f };
		world_               = b2CreateWorld(&world_def);
	}

	void Game::create_player()
	{
		assert(terrain_ && "Terrain must exist before creating the player");

		const auto spawn = terrain_->player_spawn();

		b2BodyDef body_def         = b2DefaultBodyDef();
		body_def.type              = b2_dynamicBody;
		body_def.position          = { spawn.x, spawn.y };
		body_def.linearDamping     = 2.5f;
		body_def.angularDamping    = 0.8f;
		body_def.allowFastRotation = true;
		body_def.name              = "player_ball";

		player_body_ = b2CreateBody(world_, &body_def);

		b2ShapeDef shape_def           = b2DefaultShapeDef();
		shape_def.density              = 1.1f;
		shape_def.material.friction    = 0.8f;
		shape_def.material.restitution = 0.1f;

		const b2Circle circle{ { 0.0f, 0.0f }, player_radius_ };
		b2CreateCircleShape(player_body_, &shape_def, &circle);

		player_shape_ = sf::CircleShape{ player_radius_, 40 };
		player_shape_.setOrigin({ player_radius_, player_radius_ });
		player_shape_.setFillColor(0xF29E4C_rgb);
		player_shape_.setOutlineColor(0xFFF3D9_rgb);
		player_shape_.setOutlineThickness(0.08f);
		player_shape_.setPosition(spawn);
	}

	void Game::apply_player_input()
	{
		if (!b2Body_IsValid(player_body_)) return;

		b2Vec2          force{ 0.0f, 0.0f };
		constexpr float force_strength = 35.0f;

		if (Input::is_pressed(Key::Left)) force.x -= force_strength;
		if (Input::is_pressed(Key::Right)) force.x += force_strength;
		if (Input::is_pressed(Key::Up)) force.y += force_strength;
		if (Input::is_pressed(Key::Down)) force.y -= force_strength;

		if (force.x != 0.0f || force.y != 0.0f)
		{
			b2Body_ApplyForceToCenter(player_body_, force, true);
		}
	}

	void Game::step_physics(const float dt)
	{
		if (!b2World_IsValid(world_)) return;

		physics_accumulator_ = std::min(physics_accumulator_ + dt, 0.25f);

		constexpr float fixed_step = 1.0f / 60.0f;
		constexpr int   sub_steps  = 4;

		while (physics_accumulator_ >= fixed_step)
		{
			apply_player_input();
			b2World_Step(world_, fixed_step, sub_steps);
			physics_accumulator_ -= fixed_step;
		}

		if (b2Body_IsValid(player_body_))
		{
			const auto position = b2Body_GetPosition(player_body_);
			player_shape_.setPosition({ position.x, position.y });
		}
	}

	void Game::update_world_view(const uvec2 size)
	{
		if (!terrain_ || size.x == 0 || size.y == 0) return;

		auto       min   = terrain_->display_min();
		auto       max   = terrain_->display_max();
		const auto spawn = terrain_->player_spawn();

		min.x -= 1.5f;
		min.y -= 1.5f;
		max.x += 1.5f;
		max.y = std::max(max.y + 1.5f, spawn.y + 2.0f);

		const float world_width   = std::max(max.x - min.x, 0.001f);
		const float world_height  = std::max(max.y - min.y, 0.001f);
		const float window_aspect = static_cast<float>(size.x) / static_cast<float>(size.y);

		float view_width  = world_width;
		float view_height = world_height;

		if (view_width / view_height > window_aspect)
		{
			view_height = view_width / window_aspect;
		}
		else
		{
			view_width = view_height * window_aspect;
		}

		world_view_.setCenter({ (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f });
		world_view_.setSize({ view_width, -view_height });
		window_.setView(world_view_);
	}
}
