#include "pch.hpp"
#include "Game.hpp"

#include "Input.hpp"

namespace game
{
	namespace
	{
		Game* instance{ nullptr };
		constexpr float tau = std::numbers::pi_v<float> * 2.0f;

		struct TerrainRayCastContext final
		{
			b2BodyId            ignored_body{ b2_nullBodyId };
			std::optional<vec2> hit_point{ std::nullopt };
		};

		struct GroundRayCastContext final
		{
			b2BodyId ignored_body{ b2_nullBodyId };
			bool     hit{ false };
			float    fraction{ 1.0f };
			vec2     point{ 0.0f, 0.0f };
			vec2     normal{ 0.0f, 1.0f };
		};

		b2Vec2 to_b2(const vec2& value)
		{
			return { value.x, value.y };
		}

		vec2 from_b2(const b2Vec2 value)
		{
			return { value.x, value.y };
		}

		vec2 subtract_vec2(const vec2& a, const vec2& b)
		{
			return { a.x - b.x, a.y - b.y };
		}

		vec2 scale_vec2(const vec2& value, const float scalar)
		{
			return { value.x * scalar, value.y * scalar };
		}

		vec2 normalize_vec2(const vec2& value, const vec2& fallback = { 0.0f, 1.0f })
		{
			const float length_sq = value.lengthSquared();
			if (length_sq <= 1e-8f) return fallback;
			return scale_vec2(value, 1.0f / std::sqrt(length_sq));
		}

		vec2 lerp_vec2(const vec2& a, const vec2& b, const float t)
		{
			return {
				std::lerp(a.x, b.x, t),
				std::lerp(a.y, b.y, t)
			};
		}

		float smooth_factor(const float smoothing, const float dt)
		{
			if (dt <= 0.0f) return 1.0f;
			return 1.0f - std::exp(-smoothing * dt);
		}

		float angle_from_up_direction(const vec2& up_direction)
		{
			return std::atan2(up_direction.y, up_direction.x) - std::numbers::pi_v<float> * 0.5f;
		}

		float shortest_angle_delta(const float from, const float to)
		{
			return std::remainder(to - from, tau);
		}

		std::unique_ptr<sf::ConvexShape> make_capsule_drawable(const float       radius, const float half_height,
		                                                       const std::size_t arc_segments = 12)
		{
			const auto top_arc_count = arc_segments + 1;
			auto       shape         = std::make_unique<sf::ConvexShape>(top_arc_count * 2);

			const float top_center_y    = half_height - radius;
			const float bottom_center_y = -half_height + radius;

			for (std::size_t i = 0; i <= arc_segments; ++i)
			{
				const float t     = static_cast<float>(i) / static_cast<float>(arc_segments);
				const float angle = std::numbers::pi_v<float> * (1.0f - t);
				shape->setPoint(i, {
					                std::cos(angle) * radius,
					                top_center_y + std::sin(angle) * radius
				                });
			}

			for (std::size_t i = 0; i <= arc_segments; ++i)
			{
				const float t     = static_cast<float>(i) / static_cast<float>(arc_segments);
				const float angle = -std::numbers::pi_v<float> * t;
				shape->setPoint(top_arc_count + i, {
					                std::cos(angle) * radius,
					                bottom_center_y + std::sin(angle) * radius
				                });
			}

			shape->setOrigin(shape->getLocalBounds().getCenter());
			return shape;
		}

		float terrain_ray_cast_callback(
			const b2ShapeId               shape_id, 
			const b2Vec2 point,
		                               
		const b2Vec2 /*normal*/,
		                                const float                   fraction, 
			void* context)
		{
			auto& ray_context = *static_cast<TerrainRayCastContext*>(context);
			if (B2_ID_EQUALS(b2Shape_GetBody(shape_id), ray_context.ignored_body))
			{
				return -1.0f;
			}

			ray_context.hit_point = vec2{ point.x, point.y };
			return fraction;
		}

		float ground_ray_cast_callback(
			const b2ShapeId shape_id, 
			const b2Vec2 point, 
			const b2Vec2 normal,
		                               const float     fraction, 
			void*        context)
		{
			auto& ray_context = *static_cast<GroundRayCastContext*>(context);
			if (B2_ID_EQUALS(b2Shape_GetBody(shape_id), ray_context.ignored_body))
			{
				return -1.0f;
			}

			if (b2Shape_IsSensor(shape_id))
			{
				return -1.0f;
			}

			ray_context.hit      = true;
			ray_context.fraction = fraction;
			ray_context.point    = { point.x, point.y };
			ray_context.normal   = { normal.x, normal.y };
			return fraction;
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

		if (b2Body_IsValid(player_.body))
		{
			b2DestroyBody(player_.body);
			player_.body                = b2_nullBodyId;
			player_shape_               = b2_nullShapeId;
			player_ground_sensor_shape_ = b2_nullShapeId;
		}

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

		const auto spawn = terrain_->spawn_point_from_top_center(
			player_capsule_half_height_ + player_spawn_air_clearance_);
		const auto  spawn_up    = normalize_vec2(subtract_vec2(spawn, terrain_->planet_center()));
		const float spawn_angle = angle_from_up_direction(spawn_up);

		b2BodyDef body_def         = b2DefaultBodyDef();
		body_def.type              = b2_dynamicBody;
		body_def.position          = { .x = spawn.x, .y = spawn.y };
		body_def.rotation          = b2MakeRot(spawn_angle);
		body_def.linearDamping     = 0.9f;
		body_def.angularDamping    = 8.0f;
		body_def.enableSleep       = false;
		body_def.allowFastRotation = false;
		body_def.name              = "player_capsule";

		player_                     = GameObject{};
		player_.renderable          = make_capsule_drawable(player_capsule_radius_, player_capsule_half_height_);
		player_.body                = b2CreateBody(world_, &body_def);
		player_shape_               = b2_nullShapeId;
		player_ground_sensor_shape_ = b2_nullShapeId;

		b2ShapeDef shape_def           = b2DefaultShapeDef();
		shape_def.density              = 1.25f;
		shape_def.material.friction    = 0.95f;
		shape_def.material.restitution = 0.0f;

		const float     capsule_center_offset = std::max(player_capsule_half_height_ - player_capsule_radius_, 0.01f);
		const b2Capsule capsule{
			{ 0.0f, -capsule_center_offset },
			{ 0.0f, capsule_center_offset },
			player_capsule_radius_
		};
		player_shape_ = b2CreateCapsuleShape(player_.body, &shape_def, &capsule);

		b2ShapeDef sensor_shape_def         = b2DefaultShapeDef();
		sensor_shape_def.isSensor           = true;
		sensor_shape_def.enableSensorEvents = true;
		sensor_shape_def.updateBodyMass     = false;
		sensor_shape_def.density            = 0.0f;

		const float     sensor_half_width  = player_capsule_radius_ * 0.42f;
		const float     sensor_half_height = 0.08f;
		const float     sensor_offset_y    = -player_capsule_half_height_ - sensor_half_height * 0.35f;
		const b2Polygon ground_sensor      = b2MakeOffsetBox(
			sensor_half_width,
			sensor_half_height,
			{ 0.0f, sensor_offset_y },
			b2Rot_identity);
		player_ground_sensor_shape_ = b2CreatePolygonShape(player_.body, &sensor_shape_def, &ground_sensor);

		auto& capsule_shape = dynamic_cast<sf::ConvexShape&>(*std::get<GameObject::SfDrawable>(player_.renderable));
		capsule_shape.setFillColor(0xF29E4C_rgb);
		capsule_shape.setOutlineColor(0xFFF3D9_rgb);
		capsule_shape.setOutlineThickness(0.08f);

		jump_cooldown_timer_  = 0.0f;
		player_ground_normal_ = spawn_up;
		camera_initialized_   = false;
		align_player_to_planet();
		player_.sync_from_physics();
		update_player_grounded_state();
		terrain_->update_active_colliders(player_world_position());
	}

	void Game::apply_player_input(const float fixed_step)
	{
		if (!b2Body_IsValid(player_.body)) return;
		jump_cooldown_timer_ = std::max(jump_cooldown_timer_ - fixed_step, 0.0f);

		const vec2 up_direction = player_up_direction();
		const vec2 right_direction{ up_direction.y, -up_direction.x };
		vec2       movement_direction = right_direction;
		const bool jump_held          = Input::is_pressed(Key::W) || Input::is_pressed(Key::Space);

		if (player_grounded_)
		{
			const float ground_alignment = player_ground_normal_.dot(up_direction);
			if (ground_alignment >= player_ground_min_normal_dot_)
			{
				vec2 ground_tangent{ player_ground_normal_.y, -player_ground_normal_.x };
				if (ground_tangent.dot(right_direction) < 0.0f)
				{
					ground_tangent = scale_vec2(ground_tangent, -1.0f);
				}

				movement_direction = normalize_vec2(ground_tangent, right_direction);
			}
		}

		const vec2  current_velocity      = from_b2(b2Body_GetLinearVelocity(player_.body));
		const float current_tangent_speed = current_velocity.dot(movement_direction);
		const float movement_axis         =
				(Input::is_pressed(Key::D) ? 1.0f : 0.0f) -
				(Input::is_pressed(Key::A) ? 1.0f : 0.0f);
		const float player_mass = b2Body_GetMass(player_.body);

		if (movement_axis != 0.0f)
		{
			const float desired_tangent_speed = movement_axis * player_move_speed_;
			const float max_speed_change      = player_move_acceleration_ * fixed_step;
			const float speed_change          = std::clamp(
				desired_tangent_speed - current_tangent_speed,
				-max_speed_change,
				max_speed_change);

			if (std::abs(speed_change) > 1e-4f)
			{
				const vec2 impulse = scale_vec2(movement_direction, player_mass * speed_change);
				b2Body_ApplyLinearImpulseToCenter(player_.body, to_b2(impulse), true);
			}
		}
		else if (player_grounded_)
		{
			const float brake_speed = std::min(std::abs(current_tangent_speed), player_ground_brake_ * fixed_step);
			if (brake_speed > 1e-4f)
			{
				const float direction     = current_tangent_speed > 0.0f ? -1.0f : 1.0f;
				const vec2  brake_impulse = scale_vec2(movement_direction, player_mass * brake_speed * direction);
				b2Body_ApplyLinearImpulseToCenter(player_.body, to_b2(brake_impulse), true);
			}
		}

		if (jump_held && player_grounded_ && jump_cooldown_timer_ <= 0.0f)
		{
			const vec2 jump_impulse = scale_vec2(up_direction, player_mass * player_jump_speed_);
			b2Body_ApplyLinearImpulseToCenter(player_.body, to_b2(jump_impulse), true);
			player_grounded_      = false;
			player_ground_normal_ = up_direction;
			jump_cooldown_timer_  = player_jump_cooldown_;
		}
	}

	void Game::apply_player_gravity() const
	{
		if (!b2Body_IsValid(player_.body)) return;

		const vec2 gravity_force = scale_vec2(
			player_up_direction(),
			-b2Body_GetMass(player_.body) * player_gravity_acceleration_);
		b2Body_ApplyForceToCenter(player_.body, to_b2(gravity_force), true);
	}

	void Game::update_player_grounded_state()
	{
		player_grounded_      = false;
		player_ground_normal_ = player_up_direction();

		bool sensor_grounded = false;
		if (b2Shape_IsValid(player_ground_sensor_shape_))
		{
			const int capacity = b2Shape_GetSensorCapacity(player_ground_sensor_shape_);
			if (capacity > 0)
			{
				std::vector<b2ShapeId> overlaps(static_cast<std::size_t>(capacity));
				const int overlap_count = b2Shape_GetSensorOverlaps(player_ground_sensor_shape_, overlaps.data(),
				                                                    capacity);

				for (int i = 0; i < overlap_count; ++i)
				{
					const auto overlap_shape = overlaps[static_cast<std::size_t>(i)];
					if (!b2Shape_IsValid(overlap_shape)) continue;
					if (B2_ID_EQUALS(b2Shape_GetBody(overlap_shape), player_.body)) continue;
					if (b2Shape_IsSensor(overlap_shape)) continue;

					sensor_grounded = true;
					break;
				}
			}
		}

		GroundRayCastContext ray_context{ .ignored_body = player_.body };
		const vec2           up_direction = player_up_direction();
		const vec2           ray_origin   = subtract_vec2(
			player_world_position(),
			scale_vec2(up_direction, player_capsule_half_height_ - player_capsule_radius_ * 0.35f));
		const vec2 ray_translation = scale_vec2(
			up_direction,
			-(player_capsule_radius_ + player_ground_probe_distance_));

		const b2QueryFilter filter = b2DefaultQueryFilter();
		b2World_CastRay(
			world_,
			to_b2(ray_origin),
			to_b2(ray_translation),
			filter,
			ground_ray_cast_callback,
			&ray_context);

		const bool ray_grounded = ray_context.hit &&
				normalize_vec2(ray_context.normal, up_direction).dot(up_direction) >= player_ground_min_normal_dot_;

		player_grounded_ = sensor_grounded || ray_grounded;
		if (ray_grounded)
		{
			player_ground_normal_ = normalize_vec2(ray_context.normal, up_direction);
		}
	}

	void Game::align_player_to_planet() const
	{
		if (!b2Body_IsValid(player_.body)) return;

		const auto  position      = player_world_position();
		const float target_angle  = angle_from_up_direction(player_up_direction());
		const float current_angle = b2Rot_GetAngle(b2Body_GetRotation(player_.body));

		b2Body_SetAngularVelocity(player_.body, 0.0f);
		if (std::abs(shortest_angle_delta(current_angle, target_angle)) <= 1e-4f) return;

		b2Body_SetTransform(player_.body, to_b2(position), b2MakeRot(target_angle));
	}

	void Game::configure_input()
	{
		Input::on([this](const Event::MouseWheelScrolled& scroll)
		{
			constexpr float zoom_step = 0.12f;
			camera_zoom_    = std::clamp(
				camera_zoom_ * (1.0f - scroll.delta * zoom_step),
				min_camera_zoom_,
				max_camera_zoom_);
			update_world_view(window_.getSize());
		});

		Input::on<Event::KeyPressed>(Key::P, [this]
		{
			export_current_chunk_field();
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

		update_player_grounded_state();

		while (physics_accumulator_ >= fixed_step)
		{
			align_player_to_planet();
			apply_player_gravity();
			apply_player_input(fixed_step);
			if (terrain_) terrain_->update_active_colliders(player_world_position());
			b2World_Step(world_, fixed_step, sub_steps);
			update_player_grounded_state();
			physics_accumulator_ -= fixed_step;
		}

		align_player_to_planet();
		update_player_grounded_state();
		player_.sync_from_physics();
		if (terrain_) terrain_->update_active_colliders(player_world_position());
	}

	void Game::sync_camera_to_player(const float dt)
	{
		if (!b2Body_IsValid(player_.body)) return;

		const vec2 player_position = player_world_position();
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
		if (!terrain_) return;
		if (is_water_modifier_active())
		{
			reset_terrain_tool_state(dig_tool_state_);
			reset_terrain_tool_state(place_tool_state_);
			return;
		}

		emit_terrain_tool_stamps(MouseButton::Left, dig_tool_, dig_tool_state_, dt);
		emit_terrain_tool_stamps(MouseButton::Right, place_tool_, place_tool_state_, dt);
		terrain_->apply_pending_edits();
	}

	void Game::export_current_chunk_field() const
	{
		if (!terrain_) return;

		const auto output_path = terrain_->save_chunk_field_image(player_world_position());
		if (output_path.has_value())
		{
			std::println("Saved chunk field image to {}", output_path->string());
		}
		else
		{
			std::println(std::cerr, "Failed to save chunk field image");
		}
	}

	void Game::handle_water_input(const MouseButton button)
	{
		if (!terrain_ || !is_water_modifier_active()) return;

		const auto world_position = water_tool_target_world_position();
		if (!world_position.has_value()) return;

		if (button == MouseButton::Right)
		{
			terrain_->place_water(*world_position, water_volume_cap_);
		}
		else if (button == MouseButton::Left)
		{
			terrain_->pickup_water(*world_position, water_volume_cap_);
		}
	}

	void Game::reset_terrain_tool_state(TerrainToolState& state)
	{
		state.emission_accumulator = 0.0f;
		state.last_stamp_world.reset();
	}

	void Game::emit_terrain_tool_stamps(const MouseButton button, const TerrainToolConfig& config,
	                                    TerrainToolState& state, const float               dt)
	{
		if (!terrain_) return;

		if (!Input::is_pressed(button))
		{
			state.emission_accumulator = 0.0f;
			state.last_stamp_world.reset();
			return;
		}

		const auto world_position = terrain_tool_hit_world_position();
		if (!world_position.has_value())
		{
			state.emission_accumulator = 0.0f;
			state.last_stamp_world.reset();
			return;
		}

		const auto emit_stamp = [&](const vec2 position)
		{
			terrain_->queue_edit(terrain::TerrainGenerator::TerrainEdit::make(
				position,
				config.radius,
				config.signed_strength_per_stamp,
				config.falloff_exponent));
		};

		const float stamps_per_second = std::max(config.stamps_per_second, 1.0f);
		const float interval          = 1.0f / stamps_per_second;
		state.emission_accumulator    += dt;

		if (!state.last_stamp_world.has_value())
		{
			emit_stamp(*world_position);
			state.last_stamp_world     = *world_position;
			state.emission_accumulator = std::fmod(state.emission_accumulator, interval);
			return;
		}

		const auto previous_position = *state.last_stamp_world;
		const vec2 delta{
			world_position->x - previous_position.x,
			world_position->y - previous_position.y
		};
		const float distance             = delta.length();
		const float spacing              = std::max(config.radius * config.spacing_factor, 0.05f);
		const int   time_stamp_count     = static_cast<int>(std::floor(state.emission_accumulator / interval));
		const int   movement_stamp_count = static_cast<int>(std::floor(distance / spacing));
		const int   stamp_count          = std::max(time_stamp_count, movement_stamp_count);

		if (stamp_count <= 0) return;

		if (distance <= std::numeric_limits<float>::epsilon())
		{
			for (int i = 0; i < stamp_count; ++i)
			{
				emit_stamp(*world_position);
			}
		}
		else
		{
			for (int i = 1; i <= stamp_count; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(stamp_count);
				emit_stamp(lerp_vec2(previous_position, *world_position, t));
			}
		}

		state.last_stamp_world     = *world_position;
		state.emission_accumulator = std::fmod(state.emission_accumulator, interval);
	}

	std::optional<vec2> Game::terrain_tool_hit_world_position() const
	{
		if (!terrain_ || !b2World_IsValid(world_) || !b2Body_IsValid(player_.body)) return std::nullopt;

		const auto player_world = player_world_position();
		const auto cursor_world = mouse_world_position();
		const vec2 ray_delta{
			cursor_world.x - player_world.x,
			cursor_world.y - player_world.y
		};
		const float ray_distance = ray_delta.length();
		if (ray_distance <= std::numeric_limits<float>::epsilon()) return std::nullopt;

		const auto  chunk_size       = terrain_->chunk_size();
		const float max_reach        = 0.5f * std::min(chunk_size.x, chunk_size.y);
		const float clamped_distance = std::min(ray_distance, max_reach);
		const float scale            = clamped_distance / ray_distance;
		const vec2  translation{
			ray_delta.x * scale,
			ray_delta.y * scale
		};

		TerrainRayCastContext context{ .ignored_body = player_.body };
		const b2QueryFilter   filter = b2DefaultQueryFilter();
		b2World_CastRay(
			world_,
			{ player_world.x, player_world.y },
			{ translation.x, translation.y },
			filter,
			terrain_ray_cast_callback,
			&context);

		return context.hit_point;
	}

	std::optional<vec2> Game::clamped_tool_world_position() const
	{
		if (!terrain_ || !b2Body_IsValid(player_.body)) return std::nullopt;

		const auto player_world = player_world_position();
		const auto cursor_world = mouse_world_position();
		const vec2 ray_delta{
			cursor_world.x - player_world.x,
			cursor_world.y - player_world.y
		};
		const float ray_distance = ray_delta.length();
		if (ray_distance <= std::numeric_limits<float>::epsilon()) return std::nullopt;

		const auto  chunk_size       = terrain_->chunk_size();
		const float max_reach        = 0.5f * std::min(chunk_size.x, chunk_size.y);
		const float clamped_distance = std::min(ray_distance, max_reach);
		const float scale            = clamped_distance / ray_distance;

		return vec2{
			player_world.x + ray_delta.x * scale,
			player_world.y + ray_delta.y * scale
		};
	}

	std::optional<vec2> Game::water_tool_target_world_position() const
	{
		if (const auto hit = terrain_tool_hit_world_position(); hit.has_value()) return hit;
		return clamped_tool_world_position();
	}

	vec2 Game::mouse_world_position() const
	{
		const auto pixel_position = sf::Mouse::getPosition(window_);
		const auto world_position = window_.mapPixelToCoords(pixel_position, world_view_);
		return { world_position.x, world_position.y };
	}

	vec2 Game::player_world_position() const
	{
		if (!b2Body_IsValid(player_.body)) return player_.transformable.getPosition();
		return from_b2(b2Body_GetPosition(player_.body));
	}

	vec2 Game::player_up_direction() const
	{
		const vec2 planet_center = terrain_ ? terrain_->planet_center() : vec2{ 0.0f, 0.0f };
		return normalize_vec2(subtract_vec2(player_world_position(), planet_center));
	}

	bool Game::is_water_modifier_active() const
	{
		return Input::is_pressed(Key::LControl) || Input::is_pressed(Key::RControl);
	}

	bool Game::is_player_move_input_active() const
	{
		return Input::is_pressed(Key::A) || Input::is_pressed(Key::D);
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
