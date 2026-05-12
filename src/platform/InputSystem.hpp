#pragma once

#include "pch.hpp"

namespace game::platform
{
    class InputSystem final
    {
    public:
        struct UpdateResult final
        {
            bool  should_close{ false };
            bool  resized{ false };
            uvec2 new_size{ 0, 0 };
        };

        InputSystem(const InputSystem&)                = delete;
        InputSystem& operator=(const InputSystem&)     = delete;
        InputSystem(InputSystem&&) noexcept            = delete;
        InputSystem& operator=(InputSystem&&) noexcept = delete;

        static InputSystem& instance();

        static void         begin_frame();
        static UpdateResult update(sf::Window& window);

        static bool is_pressed(Key key);
        static bool is_pressed(MouseButton button);

        static bool just_pressed(Key key);
        static bool just_pressed(MouseButton button);

        static float mouse_wheel_delta() { return instance().mouse_wheel_delta_; }

    private:
        InputSystem()  = default;
        ~InputSystem() = default;

        static constexpr std::size_t key_count{ sf::Keyboard::KeyCount };
        static constexpr std::size_t mouse_button_count{ sf::Mouse::ButtonCount };

        struct InputFrameState final
        {
            std::array<bool, key_count>          pressed_keys{};
            std::array<bool, mouse_button_count> pressed_mouse_buttons{};
        };

        static std::optional<std::size_t> key_index(Key key);
        static std::optional<std::size_t> mouse_button_index(MouseButton button);

        InputFrameState current_{};

        std::array<bool, key_count>          key_pressed_events_{};
        std::array<bool, mouse_button_count> mouse_pressed_events_{};

        float mouse_wheel_delta_{ 0.0f };
    };
}
