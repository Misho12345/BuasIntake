#include "pch.hpp"
#include "Input.hpp"

namespace game
{
	void Input::on(const Key key, callback<Event::KeyPressed> callback)
	{
		instance().key_pressed_callbacks_[key].push_back(std::move(callback));
	}

	void Input::on(const Key key, callback<Event::KeyReleased> callback)
	{
		instance().key_released_callbacks_[key].push_back(std::move(callback));
	}


	void Input::on(const MouseButton btn, callback<Event::MouseButtonPressed> callback)
	{
		instance().mouse_button_pressed_callbacks_[btn].push_back(std::move(callback));
	}

	void Input::on(const MouseButton btn, callback<Event::MouseButtonReleased> callback)
	{
		instance().mouse_button_released_callbacks_[btn].push_back(std::move(callback));
	}


	void Input::on(callback<Event::MouseWheelScrolled> callback)
	{
		instance().mouse_wheel_scrolled_callbacks_.push_back(std::move(callback));
	}

	void Input::on(callback<Event::MouseMoved> callback)
	{
		instance().mouse_moved_callbacks_.push_back(std::move(callback));
	}


	bool Input::is_pressed(const Key key)
	{
		return sf::Keyboard::isKeyPressed(key);
	}

	bool Input::is_pressed(const MouseButton button)
	{
		return sf::Mouse::isButtonPressed(button);
	}


	Input::UpdateResult Input::update(sf::Window& window)
	{
		UpdateResult result{};

		while (const auto event = window.pollEvent())
		{
			if (event->is<Event::Closed>())
			{
				window.close();
				result.should_close = true;
				return result;
			}


			if (auto* resized = event->getIf<Event::Resized>())
			{
				result.resized = true;
				result.new_size = resized->size;
				continue;
			}


			if (const auto* key = event->getIf<Event::KeyPressed>())
			{
				for (auto& callback : key_pressed_callbacks_[key->code]) callback(*key);
				for (auto& callback : key_pressed_callbacks_void_[key->code]) callback();
			}

			if (const auto* key = event->getIf<Event::KeyReleased>())
			{
				for (auto& callback : key_released_callbacks_[key->code]) callback(*key);
				for (auto& callback : key_released_callbacks_void_[key->code]) callback();
			}


			if (const auto* btn = event->getIf<Event::MouseButtonPressed>())
			{
				for (auto& callback : mouse_button_pressed_callbacks_[btn->button]) callback(*btn);
				for (auto& callback : mouse_button_pressed_callbacks_void_[btn->button]) callback();
			}

			if (const auto* btn = event->getIf<Event::MouseButtonReleased>())
			{
				for (auto& callback : mouse_button_released_callbacks_[btn->button]) callback(*btn);
				for (auto& callback : mouse_button_released_callbacks_void_[btn->button]) callback();
			}


			if (const auto* scroll = event->getIf<Event::MouseWheelScrolled>())
			{
				for (auto& callback : mouse_wheel_scrolled_callbacks_) callback(*scroll);
			}

			if (const auto* moved = event->getIf<Event::MouseMoved>())
			{
				for (auto& callback : mouse_moved_callbacks_) callback(*moved);
			}
		}

		return result;
	}


	Input& Input::instance()
	{
		static Input instance;
		return instance;
	}
}
