#pragma once

#include "pch.hpp"

namespace game::gfx
{
	enum class TextureFormat
	{
		R32F,
		RG32F,
		RGBA8,
		RGBA32F,
	};

	class Texture2D final
	{
	public:
		Texture2D() = default;
		Texture2D(uvec2 size, TextureFormat format);
		~Texture2D();

		Texture2D(const Texture2D&) = delete;
		Texture2D& operator=(const Texture2D&) = delete;
		Texture2D(Texture2D&& other) noexcept;
		Texture2D& operator=(Texture2D&& other) noexcept;

		void bind_image(GLuint unit, GLenum access) const;

		[[nodiscard]] GLuint native_handle() const;
		[[nodiscard]] uvec2 size() const;

		[[nodiscard]] static GLenum to_gl_format(TextureFormat format);

	private:
		GLuint        handle_{ 0 };
		uvec2         size_{ 0, 0 };
		TextureFormat format_{ TextureFormat::RGBA8 };
	};
}
