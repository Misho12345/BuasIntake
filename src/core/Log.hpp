#pragma once

#include <format>
#include <iostream>
#include <print>
#include <utility>

#include "core/Result.hpp"

namespace game
{
	class Log final
	{
	public:
		Log() = delete;

		template <typename... Args>
		static void info(std::format_string<Args...> fmt, Args&&... args)
		{
			std::println("[Info] {}", std::format(fmt, std::forward<Args>(args)...));
		}

		template <typename... Args>
		static void warn(std::format_string<Args...> fmt, Args&&... args)
		{
			std::println(std::cerr, "[Warning] {}", std::format(fmt, std::forward<Args>(args)...));
		}

		template <typename... Args>
		static void error(std::format_string<Args...> fmt, Args&&... args)
		{
			std::println(std::cerr, "[Error] {}", std::format(fmt, std::forward<Args>(args)...));
		}

		static void error(const Error& error)
		{
			std::println(std::cerr, "[Error] {}", error.message);
		}
	};
}
