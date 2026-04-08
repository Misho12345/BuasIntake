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
