#include "pch.hpp"
#include "Texture2D.hpp"

namespace game::gfx
{
	Result<void> Texture2D::create(const uvec2 size, const TextureFormat format)
	{
		if (size.x == 0 || size.y == 0)
		{
			return fail("Texture size must be non-zero");
		}

		if (handle_ != 0)
		{
			glDeleteTextures(1, &handle_);
			handle_ = 0;
		}

		size_ = size;
		format_ = format;

		glCreateTextures(GL_TEXTURE_2D, 1, &handle_);
		glTextureStorage2D(
			handle_,
			1,
			to_gl_format(format_),
			static_cast<GLsizei>(size_.x),
			static_cast<GLsizei>(size_.y));

		glTextureParameteri(handle_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(handle_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(handle_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(handle_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		return {};
	}

	Texture2D::~Texture2D()
	{
		if (handle_ != 0) glDeleteTextures(1, &handle_);
	}

	Texture2D::Texture2D(Texture2D&& other) noexcept :
		handle_{ std::exchange(other.handle_, 0) },
		size_{ std::exchange(other.size_, uvec2{ 0, 0 }) },
		format_{ other.format_ } {}

	Texture2D& Texture2D::operator=(Texture2D&& other) noexcept
	{
		if (this == &other) return *this;

		if (handle_ != 0)
			glDeleteTextures(1, &handle_);

		handle_ = std::exchange(other.handle_, 0);
		size_   = std::exchange(other.size_, uvec2{ 0, 0 });
		format_ = other.format_;
		return *this;
	}

	void Texture2D::bind_image(const GLuint unit, const GLenum access) const
	{
		if (handle_ == 0) return;
		glBindImageTexture(unit, handle_, 0, GL_FALSE, 0, access, to_gl_format(format_));
	}

	GLuint Texture2D::native_handle() const { return handle_; }

	uvec2 Texture2D::size() const { return size_; }
	bool Texture2D::valid() const { return handle_ != 0; }

	GLenum Texture2D::to_gl_format(const TextureFormat format)
	{
		switch (format)
		{
			case TextureFormat::R32F: return GL_R32F;
			case TextureFormat::RG32F: return GL_RG32F;
			case TextureFormat::RGBA8: return GL_RGBA8;
			case TextureFormat::RGBA32F: return GL_RGBA32F;
		}

		Log::error("Unsupported texture format requested for Texture2D");
		return GL_RGBA8;
	}
}
