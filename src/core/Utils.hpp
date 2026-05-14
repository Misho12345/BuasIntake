#pragma once

#include <algorithm>
#include <box2d/box2d.h>
#include <concepts>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <type_traits>
#include <utility>

#include <SFML/Graphics.hpp>

template <typename T>
concept numeric =
		std::integral<T> ||
		std::floating_point<T>;

template <typename T>
concept vec_supported_t =
		std::same_as<T, float> ||
		std::same_as<T, std::int32_t> ||
		std::same_as<T, std::uint32_t>;

template <vec_supported_t T> using vec2_t = sf::Vector2<T>;
template <vec_supported_t T> using vec3_t = sf::Vector3<T>;
template <vec_supported_t T> using vec4_t = sf::priv::Vector4<T>;

using vec2 = vec2_t<float>;
using vec3 = vec3_t<float>;
using vec4 = vec4_t<float>;

using ivec2 = vec2_t<std::int32_t>;
using ivec3 = vec3_t<std::int32_t>;
using ivec4 = vec4_t<std::int32_t>;

using uvec2 = vec2_t<std::uint32_t>;
using uvec3 = vec3_t<std::uint32_t>;
using uvec4 = vec4_t<std::uint32_t>;

using mat3 = sf::Glsl::Mat3;
using mat4 = sf::Glsl::Mat4;

using sf::Keyboard::Key;
using MouseButton = sf::Mouse::Button;

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
    inline constexpr float pi  = std::numbers::pi_v<float>;
    inline constexpr float tau = pi * 2.0f;
    inline constexpr float eps = std::numeric_limits<float>::epsilon();
    inline constexpr float inf = std::numeric_limits<float>::infinity();

    template <typename T1, numeric T2>
	auto operator+(const vec2_t<T1>& lhs, const T2 scalar)
	{
		using return_t = decltype(std::declval<T1>() + std::declval<T2>());
		static_assert(vec_supported_t<return_t>, "unsupported type for vector-scalar addition");

		return vec2_t<return_t>{
			static_cast<return_t>(lhs.x) + static_cast<return_t>(scalar),
			static_cast<return_t>(lhs.y) + static_cast<return_t>(scalar)
		};
	}

	template <typename T1, typename T2> requires (!std::same_as<T1, T2>) // prevent ambiguity with built-in SFML operators
    auto operator+(const vec2_t<T1>& lhs, const vec2_t<T2>& rhs)
    {
		using return_t = decltype(std::declval<T1>() + std::declval<T2>());
        static_assert(vec_supported_t<return_t>, "unsupported type for vector addition");

		return vec2_t<return_t>{
			static_cast<return_t>(lhs.x) + static_cast<return_t>(rhs.x),
			static_cast<return_t>(lhs.y) + static_cast<return_t>(rhs.y)
		};
    }


    template <typename T1, numeric T2>
	auto operator-(const vec2_t<T1>& lhs, const T2 scalar)
	{
		using return_t = decltype(std::declval<T1>() - std::declval<T2>());
		static_assert(vec_supported_t<return_t>, "unsupported type for vector-scalar subtraction");

		return vec2_t<return_t>{
			static_cast<return_t>(lhs.x) - static_cast<return_t>(scalar),
				static_cast<return_t>(lhs.y) - static_cast<return_t>(scalar)
		};
	}

	template <typename T1, typename T2> requires (!std::same_as<T1, T2>) // prevent ambiguity with built-in SFML operators
    auto operator-(const vec2_t<T1>& lhs, const vec2_t<T2>& rhs)
	{
		using return_t = decltype(std::declval<T1>() - std::declval<T2>());
        static_assert(vec_supported_t<return_t>, "unsupported type for vector subtraction");

        return vec2_t<return_t>{
	        static_cast<return_t>(lhs.x) - static_cast<return_t>(rhs.x),
	        static_cast<return_t>(lhs.y) - static_cast<return_t>(rhs.y)
        };
    }


	template <typename T1, numeric T2> requires (!std::same_as<T1, T2>) // prevent ambiguity with built-in SFML operators
    auto operator*(const vec2_t<T1>& value, const T2 scalar)
    {
	    using return_t = decltype(std::declval<T1>() * std::declval<T2>());
	    static_assert(vec_supported_t<return_t>, "unsupported type for vector-scalar multiplication");

	    return vec2_t<return_t>{
		    static_cast<return_t>(value.x) * static_cast<return_t>(scalar),
		    static_cast<return_t>(value.y) * static_cast<return_t>(scalar)
	    };
    }

	template <typename T1, typename T2>
	auto operator*(const vec2_t<T1>& lhs, const vec2_t<T2>& rhs)
	{
		using return_t = decltype(std::declval<T1>() * std::declval<T2>());
		static_assert(vec_supported_t<return_t>, "unsupported type for vector component-wise multiplication");

		return vec2_t<return_t>{
			static_cast<return_t>(lhs.x) * static_cast<return_t>(rhs.x),
			static_cast<return_t>(lhs.y) * static_cast<return_t>(rhs.y)
		};
	}

	template <numeric T1, typename T2> requires (!std::same_as<T1, T2>) // prevent ambiguity with built-in SFML operators
    auto operator*(const T1 scalar, const vec2_t<T2>& value)
    {
	    using return_t = decltype(std::declval<T1>() * std::declval<T2>());
	    static_assert(vec_supported_t<return_t>, "unsupported type for vector-scalar multiplication");

	    return vec2_t<return_t>{
		    static_cast<return_t>(scalar) * static_cast<return_t>(value.x),
		    static_cast<return_t>(scalar) * static_cast<return_t>(value.y)
	    };
    }


    template <typename T1, typename T2>
    auto operator/(const vec2_t<T1>& lhs, const vec2_t<T2>& rhs)
    {
		using return_t = decltype(std::declval<T1>() / std::declval<T2>());
		static_assert(vec_supported_t<return_t>, "unsupported type for vector component-wise division");

		return vec2_t<return_t>{
			static_cast<return_t>(lhs.x) / static_cast<return_t>(rhs.x),
			static_cast<return_t>(lhs.y) / static_cast<return_t>(rhs.y)
		};
    }


    template <typename T1, numeric T2>
	vec2_t<T1>& operator+=(vec2_t<T1>& value, const T2 scalar)
	{
		value.x += static_cast<T1>(scalar);
		value.y += static_cast<T1>(scalar);
		return value;
	}

	template <typename T1, typename T2> requires (!std::same_as<T1, T2>) // prevent ambiguity with built-in SFML operators
    vec2_t<T1>& operator+=(vec2_t<T1>& lhs, const vec2_t<T2>& rhs)
    {
        lhs.x += static_cast<T1>(rhs.x);
        lhs.y += static_cast<T1>(rhs.y);
        return lhs;
    }


	template <typename T1, numeric T2>
	vec2_t<T1>& operator-=(vec2_t<T1>& value, const T2 scalar)
	{
		value.x -= static_cast<T1>(scalar);
		value.y -= static_cast<T1>(scalar);
		return value;
	}

	template <typename T1, typename T2> requires (!std::same_as<T1, T2>) // prevent ambiguity with built-in SFML operators
    vec2_t<T1>& operator-=(vec2_t<T1>& lhs, const vec2_t<T2>& rhs)
    {
        lhs.x -= static_cast<T1>(rhs.x);
        lhs.y -= static_cast<T1>(rhs.y);
        return lhs;
    }


	template <typename T1, numeric T2> requires (!std::same_as<T1, T2>) // prevent ambiguity with built-in SFML operators
	vec2_t<T1>& operator*=(vec2_t<T1>& value, const T2 scalar)
	{
		value.x *= static_cast<T1>(scalar);
		value.y *= static_cast<T1>(scalar);
		return value;
	}

	template <typename T1, typename T2>
    vec2_t<T1>& operator*=(vec2_t<T1>& lhs, const vec2_t<T2>& rhs)
    {
        lhs.x *= static_cast<T1>(rhs.x);
        lhs.y *= static_cast<T1>(rhs.y);
        return lhs;
    }


    template <typename T1, typename T2>
	vec2_t<T1>& operator/=(vec2_t<T1>& lhs, const vec2_t<T2>& rhs)
	{
		lhs.x /= static_cast<T1>(rhs.x);
		lhs.y /= static_cast<T1>(rhs.y);
		return lhs;
	}


    inline b2Vec2 to_b2(const vec2& value) { return { value.x, value.y }; }
    inline vec2   from_b2(const b2Vec2 value) { return { value.x, value.y }; }

    template <typename T>
    vec2_t<T> normalize(const vec2_t<T>& value, const vec2_t<T>& fallback = { 0.0f, 1.0f })
    {
        const float length_sq = value.lengthSquared();
        if (length_sq <= 1e-8f) return fallback;
        return value / std::sqrt(length_sq);
    }

    template <typename T>
    float distance(const vec2_t<T>& a, const vec2_t<T>& b)
    {
        return (a - b).length();
    }

    template <typename T>
    vec2_t<T> lerp(const vec2_t<T>& a, const vec2_t<T>& b, const float t)
    {
        return {
            std::lerp(a.x, b.x, t),
            std::lerp(a.y, b.y, t)
        };
    }

    template <typename T> T min(const vec2_t<T>& vec) { return std::min(vec.x, vec.y); }
    template <typename T> T max(const vec2_t<T>& vec) { return std::max(vec.x, vec.y); }

    template <typename T> T min(const vec3_t<T>& vec) { return std::min({ vec.x, vec.y, vec.z }); }
    template <typename T> T max(const vec3_t<T>& vec) { return std::max({ vec.x, vec.y, vec.z }); }

    template <typename T> T min(const vec4_t<T>& vec) { return std::min({ vec.x, vec.y, vec.z, vec.w }); }
    template <typename T> T max(const vec4_t<T>& vec) { return std::max({ vec.x, vec.y, vec.z, vec.w }); }


    template <typename T, typename Func, typename... Args> requires std::invocable<Func, T, Args...>
	vec2_t<T> vec2_each(const vec2_t<T>& vec, Func&& func, Args&&... args)
    {
        return vec2_t<T>{
	        static_cast<T>(std::invoke(func, vec.x, std::forward<Args>(args)...)),
	        static_cast<T>(std::invoke(func, vec.y, std::forward<Args>(args)...))
        };
    }


    inline float smooth_factor(const float smoothing, const float dt)
    {
        if (dt <= 0.0f) return 1.0f;
        return 1.0f - std::exp(-smoothing * dt);
    }

    template <typename T>
    float dir_to_angle(const vec2_t<T>& dir) { return std::atan2(dir.y, dir.x) - pi * 0.5f; }

    inline float shortest_angle_delta(const float from, const float to) { return std::remainder(to - from, tau); }

    inline std::uint64_t sample_key(const ivec2 coord)
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32u) |
                static_cast<std::uint32_t>(coord.y);
    }
}
