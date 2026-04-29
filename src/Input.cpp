#include "pch.hpp"
#include "Input.hpp"

namespace game
{
	namespace
	{
		template <typename KeyT, typename EventT, typename CallbackMapT, typename VoidCallbackMapT>
		void dispatch_keyed_callbacks(
			CallbackMapT& callbacks,
			VoidCallbackMapT& void_callbacks,
			const KeyT key,
			const EventT& event)
		{
			if (const auto callback_it = callbacks.find(key); callback_it != callbacks.end())
			{
				for (auto& callback : callback_it->second) callback(event);
			}

			if (const auto void_callback_it = void_callbacks.find(key); void_callback_it != void_callbacks.end())
			{
				for (auto& callback : void_callback_it->second) callback();
			}
		}

		template <typename EventT, typename CallbackListT>
		void dispatch_callbacks(CallbackListT& callbacks, const EventT& event)
		{
			for (auto& callback : callbacks) callback(event);
		}
	}

	void Input::on(const Key key, callback<Event::KeyPressed> callback)
	{
		instance().key_pressed_callbacks_[key].push_back(std::move(callback));
	}


	void Input::on(const MouseButton btn, callback<Event::MouseButtonPressed> callback)
	{
		instance().mouse_button_pressed_callbacks_[btn].push_back(std::move(callback));
	}


	void Input::on(callback<Event::MouseWheelScrolled> callback)
	{
		instance().mouse_wheel_scrolled_callbacks_.push_back(std::move(callback));
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
				dispatch_keyed_callbacks(
					key_pressed_callbacks_,
					key_pressed_callbacks_void_,
					key->code,
					*key);
			}


			if (const auto* btn = event->getIf<Event::MouseButtonPressed>())
			{
				dispatch_keyed_callbacks(
					mouse_button_pressed_callbacks_,
					mouse_button_pressed_callbacks_void_,
					btn->button,
					*btn);
			}


			if (const auto* scroll = event->getIf<Event::MouseWheelScrolled>())
			{
				dispatch_callbacks(mouse_wheel_scrolled_callbacks_, *scroll);
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
