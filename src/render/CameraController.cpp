#include "pch.hpp"

#include "render/CameraController.hpp"

namespace game::render
{
    void CameraController::set_world_span(const vec2 world_span) noexcept
    {
        settings_.world_span = world_span;
    }

    void CameraController::reset_follow() noexcept
    {
        follow_initialized_ = false;
    }

    void CameraController::zoom_by_scroll(const float scroll_delta) noexcept
    {
        constexpr float zoom_step = 0.12f;
        zoom_ = std::clamp(zoom_ * (1.0f - scroll_delta * zoom_step), settings_.min_zoom, settings_.max_zoom);
    }

    void CameraController::update_view_size(const uvec2 size)
    {
        if (size.x == 0 || size.y == 0) return;

        const float window_aspect = static_cast<float>(size.x) / static_cast<float>(size.y);

        float view_width = std::max(settings_.world_span.x * zoom_, 0.001f);
        float view_height = std::max(settings_.world_span.y * zoom_, 0.001f);

        if (view_width / view_height > window_aspect) view_height = view_width / window_aspect;
        else view_width = view_height * window_aspect;

        view_.setSize({ view_width, -view_height });
    }

    void CameraController::sync_to_player(const world::World& world, const float dt, const bool move_input_active)
    {
        if (!world.ready()) return;

        const vec2 player_position = world.player().world_position();
        if (!follow_initialized_)
        {
            focus_world_ = player_position;
            rotation_radians_ = dir_to_angle(player_up_dir(world));
            follow_initialized_ = true;
        }

        vec2 desired_focus = focus_world_;
        const vec2 player_delta = player_position - focus_world_;
        const float follow_threshold_sq = settings_.follow_threshold * settings_.follow_threshold;

        if (move_input_active)
        {
            // Give the player a small dead zone before the camera starts chasing them.
            const float distance_sq = player_delta.lengthSquared();
            if (distance_sq > follow_threshold_sq)
            {
                desired_focus = player_position - normalize(player_delta, { 1.0f, 0.0f }) * settings_.follow_threshold;
            }
        }
        else desired_focus = player_position;

        const float position_alpha = smooth_factor(move_input_active ? settings_.follow_smoothing : settings_.recenter_smoothing, dt);
        focus_world_ = lerp(focus_world_, desired_focus, position_alpha);

        const vec2 planet_center = world.terrain().planet_center();
        const bool camera_is_actively_repositioning = !move_input_active || player_delta.lengthSquared() > follow_threshold_sq;
        if (camera_is_actively_repositioning)
        {
            const vec2 camera_up_dir = normalize(focus_world_ - planet_center, player_up_dir(world));
            const float target_rotation = dir_to_angle(camera_up_dir);
            const float rotation_delta = shortest_angle_delta(rotation_radians_, target_rotation);
            if (std::abs(rotation_delta) > settings_.rotation_deadzone_radians)
            {
                rotation_radians_ += rotation_delta * smooth_factor(settings_.rotation_smoothing, dt);
            }
        }

        view_.setCenter(focus_world_);
        view_.setRotation(sf::radians(rotation_radians_));
    }

    vec2 CameraController::mouse_world_position(const sf::RenderWindow& window) const
    {
        const auto pixel_position = sf::Mouse::getPosition(window);
        const auto world_position = window.mapPixelToCoords(pixel_position, view_);
        return { world_position.x, world_position.y };
    }

    vec2 CameraController::player_up_dir(const world::World& world) const
    {
        if (!world.ready()) return { 0.0f, 1.0f };
        return world.player().up_direction(world.terrain().planet_center());
    }
}
