#pragma once

#include "pch.hpp"

#include "world/World.hpp"

namespace game::render
{
    class CameraController final
    {
    public:
        void set_world_span(vec2 world_span) noexcept;
        void reset_follow() noexcept;
        void zoom_by_scroll(float scroll_delta) noexcept;
        void update_view_size(uvec2 size);
        void sync_to_player(const world::World& world, float dt, bool move_input_active);

        [[nodiscard]] const sf::View& view() const noexcept { return view_; }
        [[nodiscard]] vec2 mouse_world_position(const sf::RenderWindow& window) const;

    private:
        struct Settings final
        {
            vec2 world_span{ 36.0f, 27.0f };
            float min_zoom{ 0.05f };
            float max_zoom{ 7.5f };
            float follow_threshold{ 3.25f };
            float follow_smoothing{ 10.0f };
            float recenter_smoothing{ 5.0f };
            float rotation_smoothing{ 7.5f };
            float rotation_deadzone_radians{ 0.0065f };
        };

        [[nodiscard]] vec2 player_up_dir(const world::World& world) const;

        Settings settings_{};
        sf::View view_{};
        float zoom_{ 1.0f };
        bool follow_initialized_{ false };
        vec2 focus_world_{ 0.0f, 0.0f };
        float rotation_radians_{ 0.0f };
    };
}
