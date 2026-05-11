#include "pch.hpp"

#include <utility>

#include "platform/InputSystem.hpp"

namespace game::platform
{
    InputSystem& InputSystem::instance()
    {
        static InputSystem input;
        return input;
    }

    void InputSystem::begin_frame()
    {
        InputSystem& input = instance();

        input.key_pressed_events_.fill(false);
        input.key_released_events_.fill(false);

        input.mouse_pressed_events_.fill(false);
        input.mouse_released_events_.fill(false);

        input.mouse_wheel_delta_ = 0.0f;
    }

    InputSystem::UpdateResult InputSystem::update(sf::Window& window)
    {
        InputSystem& input = instance();
        UpdateResult result{};

        while (const auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
                result.should_close = true;
                return result;
            }

            if (auto* resized = event->getIf<sf::Event::Resized>())
            {
                result.resized = true;
                result.new_size = resized->size;
                continue;
            }

            if (event->is<sf::Event::FocusLost>())
            {
                input.current_ = {};

                input.key_pressed_events_.fill(false);
                input.key_released_events_.fill(false);

                input.mouse_pressed_events_.fill(false);
                input.mouse_released_events_.fill(false);

                input.mouse_wheel_delta_ = 0.0f;

                continue;
            }

            if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                if (const auto index = key_index(key->code); index.has_value())
                {
                    input.current_.pressed_keys[*index] = true;
                    input.key_pressed_events_[*index] = true;
                }

                continue;
            }

            if (const auto* key = event->getIf<sf::Event::KeyReleased>())
            {
                if (const auto index = key_index(key->code); index.has_value())
                {
                    input.current_.pressed_keys[*index] = false;
                    input.key_released_events_[*index] = true;
                }

                continue;
            }

            if (const auto* btn = event->getIf<sf::Event::MouseButtonPressed>())
            {
                if (const auto index = mouse_button_index(btn->button); index.has_value())
                {
                    input.current_.pressed_mouse_buttons[*index] = true;
                    input.mouse_pressed_events_[*index] = true;
                }

                continue;
            }

            if (const auto* btn = event->getIf<sf::Event::MouseButtonReleased>())
            {
                if (const auto index = mouse_button_index(btn->button); index.has_value())
                {
                    input.current_.pressed_mouse_buttons[*index] = false;
                    input.mouse_released_events_[*index] = true;
                }

                continue;
            }

            if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>())
            {
                input.mouse_wheel_delta_ += scroll->delta;
            }
        }

        return result;
    }

    bool InputSystem::is_pressed(const Key key)
    {
        const InputSystem& input = instance();
        const auto index = key_index(key);
        if (!index.has_value()) return false;
        return input.current_.pressed_keys[*index];
    }

    bool InputSystem::is_pressed(const MouseButton button)
    {
        const InputSystem& input = instance();
        const auto index = mouse_button_index(button);
        if (!index.has_value()) return false;
        return input.current_.pressed_mouse_buttons[*index];
    }

    bool InputSystem::just_pressed(const Key key)
    {
        const InputSystem& input = instance();
        const auto index = key_index(key);
        if (!index.has_value()) return false;
        return input.key_pressed_events_[*index];
    }

    bool InputSystem::just_pressed(const MouseButton button)
    {
        const InputSystem& input = instance();
        const auto index = mouse_button_index(button);
        if (!index.has_value()) return false;
        return input.mouse_pressed_events_[*index];
    }

    bool InputSystem::released(const Key key)
    {
        const InputSystem& input = instance();
        const auto index = key_index(key);
        if (!index.has_value()) return false;
        return input.key_released_events_[*index];
    }

    bool InputSystem::released(const MouseButton button)
    {
        const InputSystem& input = instance();
        const auto index = mouse_button_index(button);
        if (!index.has_value()) return false;
        return input.mouse_released_events_[*index];
    }

    std::optional<std::size_t> InputSystem::key_index(const Key key) noexcept
    {
        const int index = static_cast<int>(key);
        if (index < 0 || std::cmp_greater_equal(index, key_count)) return std::nullopt;
        return static_cast<std::size_t>(index);
    }

    std::optional<std::size_t> InputSystem::mouse_button_index(const MouseButton button) noexcept
    {
        const int index = static_cast<int>(button);
        if (index < 0 || std::cmp_greater_equal(index, mouse_button_count)) return std::nullopt;
        return static_cast<std::size_t>(index);
    }
}
