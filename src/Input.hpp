#pragma once
#include "pch.hpp"

using sf::Event;

namespace game
{
	class Input final
	{
		template <typename E>
		using callback = std::move_only_function<void(const E&)>;

		using callback_void = std::move_only_function<void()>;

	public:
		static void on(Key key, callback<Event::KeyPressed> callback);
		static void on(Key key, callback<Event::KeyReleased> callback);

		template <typename T> requires std::same_as<T, Event::KeyPressed>
		static void on(Key key, callback_void callback);

		template <typename T> requires std::same_as<T, Event::KeyReleased>
		static void on(Key key, callback_void callback);


		static void on(MouseButton btn, callback<Event::MouseButtonPressed> callback);
		static void on(MouseButton btn, callback<Event::MouseButtonReleased> callback);

		template <typename T> requires std::same_as<T, Event::MouseButtonPressed>
		static void on(MouseButton btn, callback_void callback);

		template <typename T> requires std::same_as<T, Event::MouseButtonReleased>
		static void on(MouseButton btn, callback_void callback);


		static void on(callback<Event::MouseWheelScrolled> callback);
		static void on(callback<Event::MouseMoved> callback);

		[[nodiscard]] static bool is_pressed(Key key);
		[[nodiscard]] static bool is_pressed(MouseButton button);

	private:
		Input() = default;
		static Input& instance();

		struct UpdateResult
		{
			bool should_close{ false };
			bool resized{ false };
			uvec2 new_size{ 0, 0 };
		};

		UpdateResult update(sf::Window& window);

		template <typename E>
		using callbacks = std::vector<callback<E>>;

		template <typename T, typename E>
		using callbacks_map = std::unordered_map<T, callbacks<E>>;

		template <typename T>
		using callbacks_void_map = std::unordered_map<T, std::vector<callback_void>>;


		callbacks_map<Key, Event::KeyPressed> key_pressed_callbacks_;
		callbacks_map<Key, Event::KeyReleased> key_released_callbacks_;

		callbacks_void_map<Key> key_pressed_callbacks_void_;
		callbacks_void_map<Key> key_released_callbacks_void_;


		callbacks_map<MouseButton, Event::MouseButtonPressed> mouse_button_pressed_callbacks_;
		callbacks_map<MouseButton, Event::MouseButtonReleased> mouse_button_released_callbacks_;

		callbacks_void_map<MouseButton> mouse_button_pressed_callbacks_void_;
		callbacks_void_map<MouseButton> mouse_button_released_callbacks_void_;


		callbacks<Event::MouseWheelScrolled> mouse_wheel_scrolled_callbacks_;
		callbacks<Event::MouseMoved> mouse_moved_callbacks_;

		friend class Game;
	};


	template <typename T> requires std::same_as<T, Event::KeyPressed>
	void Input::on(const Key key, callback_void callback)
	{
		instance().key_pressed_callbacks_void_[key].push_back(std::move(callback));
	}

	template <typename T> requires std::same_as<T, Event::KeyReleased>
	void Input::on(const Key key, callback_void callback)
	{
		instance().key_released_callbacks_void_[key].push_back(std::move(callback));
	}


	template <typename T> requires std::same_as<T, Event::MouseButtonPressed>
	void Input::on(const MouseButton btn, callback_void callback)
	{
		instance().mouse_button_pressed_callbacks_void_[btn].push_back(std::move(callback));
	}

	template <typename T> requires std::same_as<T, Event::MouseButtonReleased>
	void Input::on(const MouseButton btn, callback_void callback)
	{
		instance().mouse_button_released_callbacks_void_[btn].push_back(std::move(callback));
	}
}
