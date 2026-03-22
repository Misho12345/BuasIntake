#pragma once

#include "pch.hpp"

namespace game::gfx
{
	class SSBO final
	{
	public:
		explicit SSBO(const GLenum usage = GL_DYNAMIC_COPY) : usage_{ usage }
		{
			glCreateBuffers(1, &id_);
		}

		~SSBO()
		{
			if (id_ != 0) glDeleteBuffers(1, &id_);
		}

		SSBO(const SSBO&)            = delete;
		SSBO& operator=(const SSBO&) = delete;

		SSBO(SSBO&& other) noexcept
			: id_{ std::exchange(other.id_, 0) },
			  usage_{ other.usage_ },
			  size_bytes_{ std::exchange(other.size_bytes_, 0) } {}

		SSBO& operator=(SSBO&& other) noexcept
		{
			if (this == &other) return *this;
			if (id_ != 0) glDeleteBuffers(1, &id_);

			id_         = std::exchange(other.id_, 0);
			usage_      = other.usage_;
			size_bytes_ = std::exchange(other.size_bytes_, 0);
			return *this;
		}

		template <typename T> requires std::is_trivially_copyable_v<T>
		void resize(const std::size_t count) { resize_bytes(sizeof(T) * count); }

		template <typename T> requires std::is_trivially_copyable_v<T>
		void set_data(const std::span<const T> data)
		{
			resize_bytes(data.size_bytes(), data.empty() ? nullptr : data.data());
		}

		template <typename T> requires std::is_trivially_copyable_v<T>
		void write(const std::span<const T> data, const std::size_t offset_elements = 0)
		{
			const auto offset = offset_elements * sizeof(T);
			assert(offset + data.size_bytes() <= size_bytes_ && "Write exceeds buffer size");
			glNamedBufferSubData(id_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(data.size_bytes()), data.data());
		}

		template <typename T> requires std::is_trivially_copyable_v<T>
		[[nodiscard]]
		std::vector<T> read(const std::size_t count, const std::size_t offset_elements = 0) const
		{
			std::vector<T> output(count);
			if (count == 0) return output;

			const auto offset = offset_elements * sizeof(T);
			assert(offset + sizeof(T) * count <= size_bytes_ && "Read exceeds buffer size");
			glGetNamedBufferSubData(id_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(sizeof(T) * count), output.data());
			return output;
		}

		template <typename T> requires std::is_trivially_copyable_v<T>
		[[nodiscard]]
		T read_one(const std::size_t offset_elements = 0) const
		{
			T          output{};
			const auto offset = offset_elements * sizeof(T);
			assert(offset + sizeof(T) <= size_bytes_ && "Read exceeds buffer size");
			glGetNamedBufferSubData(id_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(sizeof(T)), &output);
			return output;
		}

		void bind_base(const GLuint binding) const
		{
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id_);
		}

		[[nodiscard]] GLuint id() const { return id_; }

	private:
		void resize_bytes(const std::size_t byte_count, const void* data = nullptr)
		{
			size_bytes_ = byte_count;
			glNamedBufferData(id_, static_cast<GLsizeiptr>(byte_count), data, usage_);
		}

		GLuint      id_{ 0 };
		GLenum      usage_{ GL_DYNAMIC_COPY };
		std::size_t size_bytes_{ 0 };
	};
}
