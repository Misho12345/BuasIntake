#pragma once

#include "pch.hpp"

#include "world/World.hpp"

namespace game::render
{
    class CameraController final
    {
    public:
        void set_world_span(vec2 world_span);
        void zoom_by_scroll(float scroll_delta);
        void update_view_size(uvec2 size);

        // this is the follow and rotate logic for the camera and it is called every frame after simulation
        void sync_to_player(const world::World& world, float dt, bool move_input_active);

        const sf::View& view() const { return view_; }
        vec2            mouse_world_position(const sf::RenderWindow& window) const;

    private:
        struct Settings final
        {
            vec2  world_span{ 36.0f, 27.0f };
            float min_zoom{ 0.05f };
            float max_zoom{ 7.5f };
            float follow_threshold{ 3.25f };
            float follow_smoothing{ 10.0f };
            float recenter_smoothing{ 5.0f };
            float rotation_smoothing{ 7.5f };
            float rotation_deadzone_radians{ 0.0065f };
        };

        // the whole camera tilt logic depends on this one up vector that relative to the planet's center
        vec2 player_up_dir(const world::World& world) const;

        Settings settings_{};
        sf::View view_{};
        float    zoom_{ 1.0f };
        bool     follow_initialized_{ false };
        vec2     focus_world_{ 0.0f, 0.0f };
        float    rotation_radians_{ 0.0f };
    };
}
