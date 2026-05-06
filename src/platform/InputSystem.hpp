#pragma once

#include "pch.hpp"

namespace game::platform
{
    class InputSystem final
    {
    public:
        struct UpdateResult final
        {
            bool should_close{ false };
            bool resized{ false };
            uvec2 new_size{ 0, 0 };
        };

        void begin_frame();
        UpdateResult update(sf::Window& window);

        bool is_pressed(Key key) const;
        bool is_pressed(MouseButton button) const;
        bool just_pressed(Key key) const;
        bool just_pressed(MouseButton button) const;
        bool released(Key key) const;
        bool released(MouseButton button) const;
        float mouse_wheel_delta() const noexcept { return mouse_wheel_delta_; }

    private:
        static constexpr std::size_t key_count{ sf::Keyboard::KeyCount };
        static constexpr std::size_t mouse_button_count{ sf::Mouse::ButtonCount };

        struct InputFrameState final
        {
            std::array<bool, key_count> pressed_keys{};
            std::array<bool, mouse_button_count> pressed_mouse_buttons{};
        };

        static std::optional<std::size_t> key_index(Key key) noexcept;
        static std::optional<std::size_t> mouse_button_index(MouseButton button) noexcept;

        InputFrameState current_{};
        std::array<bool, key_count> key_pressed_events_{};
        std::array<bool, key_count> key_released_events_{};
        std::array<bool, mouse_button_count> mouse_pressed_events_{};
        std::array<bool, mouse_button_count> mouse_released_events_{};
        float mouse_wheel_delta_{ 0.0f };
    };
}
