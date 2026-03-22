#pragma once

#include "pch.hpp"

namespace game::gfx
{
	[[nodiscard]] 
	inline mat4 make_projection(const sf::View& view)
	{
		const auto center = view.getCenter();
		const auto size = view.getSize();

		const float left = center.x - size.x * 0.5f;
		const float right = center.x + size.x * 0.5f;
		const float bottom = center.y + size.y * 0.5f;
		const float top = center.y - size.y * 0.5f;

		const float inv_width = 1.0f / std::max(right - left, 0.001f);
		const float inv_height = 1.0f / std::max(top - bottom, 0.001f);

		const std::array proj
		{
			2.0f * inv_width, 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f * inv_height, 0.0f, 0.0f,
			0.0f, 0.0f, -1.0f, 0.0f,
			-(right + left) * inv_width, -(top + bottom) * inv_height, 0.0f, 1.0f
		};

		return mat4{ proj.data() };
	}
}
