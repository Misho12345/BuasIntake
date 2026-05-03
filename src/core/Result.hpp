#pragma once

#include <concepts>
#include <expected>
#include <format>
#include <string>
#include <utility>

namespace game
{
	struct Error final
	{
		explicit Error(std::string message) : message{ std::move(message) } {}

		template <typename... Args>
		explicit Error(std::format_string<Args...> fmt, Args&&... args)
			: message{ std::format(fmt, std::forward<Args>(args)...) } {}

		std::string message{};
	};

	template <typename T> requires (!std::same_as<T, Error>)
	using Result = std::expected<T, Error>;

	[[nodiscard]]
	inline std::unexpected<Error> fail(Error error)
	{
		return std::unexpected<Error>{ std::move(error) };
	}

	template <typename... Args>
	[[nodiscard]]
	inline std::unexpected<Error> fail(std::format_string<Args...> fmt, Args&&... args)
	{
		return std::unexpected<Error>{
			Error{ fmt, std::forward<Args>(args)... }
		};
	}
}

#define TRY(expr) \
	do \
	{ \
		auto _res = (expr); \
		if (!_res) return ::game::fail(std::move(_res).error()); \
	} while (false)
