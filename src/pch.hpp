#pragma once


// ----- STL -----
#include <print>
#include <format>
#include <iostream>
#include <fstream>
#include <sstream>

#include <filesystem>
namespace fs = std::filesystem;

#include <vector>
#include <array>
#include <span>
#include <queue>

#include <unordered_map>
#include <unordered_set>

#include <string>
#include <string_view>

#include <memory>
#include <utility>
#include <optional>
#include <variant>
#include <functional>
#include <stdexcept>

#include <algorithm>
#include <numeric>
#include <ranges>
#include <chrono>
#include <numbers>
#include <limits>
#include <initializer_list>

#include <concepts>
#include <type_traits>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>


// ----- Box2D -----
#include <box2d/box2d.h>


// ----- GLAD -----
#include <glad/gl.h>


// ----- SFML -----
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

using vec2 = sf::Glsl::Vec2;
using vec3 = sf::Glsl::Vec3;
using vec4 = sf::Glsl::Vec4;

using ivec2 = sf::Glsl::Ivec2;
using ivec3 = sf::Glsl::Ivec3;
using ivec4 = sf::Glsl::Ivec4;

using uvec2 = sf::Vector2u;
using uvec3 = sf::Vector3<std::uint32_t>;
using uvec4 = sf::priv::Vector4<std::uint32_t>;

using mat3 = sf::Glsl::Mat3;
using mat4 = sf::Glsl::Mat4;

using sf::Keyboard::Key;
using MouseButton = sf::Mouse::Button;


inline sf::Angle operator""_deg(const long double val)
{
	return sf::degrees(static_cast<float>(val));
}

inline sf::Angle operator""_rad(const long double val)
{
	return sf::radians(static_cast<float>(val));
}


inline sf::Time operator""_sec(const long double val)
{
	return sf::seconds(static_cast<float>(val));
}

inline sf::Time operator""_ms(const unsigned long long val)
{
	return sf::milliseconds(static_cast<std::int32_t>(val));
}

inline sf::Time operator""_us(const unsigned long long val)
{
	return sf::microseconds(static_cast<std::int64_t>(val));
}


inline sf::Color operator""_rgb(const unsigned long long val)
{
	return {
		static_cast<std::uint8_t>(val >> 16 & 0xFF),
		static_cast<std::uint8_t>(val >> 8 & 0xFF),
		static_cast<std::uint8_t>(val & 0xFF)
	};
}

inline sf::Color operator""_rgba(const unsigned long long val)
{
	return {
		static_cast<std::uint8_t>(val >> 24 & 0xFF),
		static_cast<std::uint8_t>(val >> 16 & 0xFF),
		static_cast<std::uint8_t>(val >> 8 & 0xFF),
		static_cast<std::uint8_t>(val & 0xFF)
	};
}


namespace game
{
	inline constexpr float tau = std::numbers::pi_v<float> * 2.0f;

	[[nodiscard]]
	inline b2Vec2 to_b2(const vec2& value)
	{
		return { value.x, value.y };
	}

	[[nodiscard]]
	inline vec2 from_b2(const b2Vec2 value)
	{
		return { value.x, value.y };
	}

	[[nodiscard]]
	inline vec2 add_vec2(const vec2& a, const vec2& b)
	{
		return { a.x + b.x, a.y + b.y };
	}

	[[nodiscard]]
	inline vec2 subtract_vec2(const vec2& a, const vec2& b)
	{
		return { a.x - b.x, a.y - b.y };
	}

	[[nodiscard]]
	inline vec2 scale_vec2(const vec2& value, const float scalar)
	{
		return { value.x * scalar, value.y * scalar };
	}

	[[nodiscard]]
	inline vec2 normalize_vec2(const vec2& value, const vec2& fallback = { 0.0f, 1.0f })
	{
		const float length_sq = value.lengthSquared();
		if (length_sq <= 1e-8f) return fallback;
		return scale_vec2(value, 1.0f / std::sqrt(length_sq));
	}

	[[nodiscard]]
	inline vec2 lerp_vec2(const vec2& a, const vec2& b, const float t)
	{
		return {
			std::lerp(a.x, b.x, t),
			std::lerp(a.y, b.y, t)
		};
	}

	[[nodiscard]]
	inline float smooth_factor(const float smoothing, const float dt)
	{
		if (dt <= 0.0f) return 1.0f;
		return 1.0f - std::exp(-smoothing * dt);
	}

	[[nodiscard]]
	inline float angle_from_up_direction(const vec2& up_direction)
	{
		return std::atan2(up_direction.y, up_direction.x) - std::numbers::pi_v<float> * 0.5f;
	}

	[[nodiscard]]
	inline float shortest_angle_delta(const float from, const float to)
	{
		return std::remainder(to - from, tau);
	}
}
