#pragma once

#include "pch.hpp"

namespace game::gfx
{
	struct Vertex final
	{
		vec2 position{};
		std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	class Mesh final
	{
	public:
		Mesh();
		~Mesh();

		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;
		Mesh(Mesh&& other) noexcept;
		Mesh& operator=(Mesh&& other) noexcept;

		void set_data(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
		void draw() const;

		[[nodiscard]] bool empty() const;

	private:
		GLuint vao_{ 0 };
		GLuint vbo_{ 0 };
		GLuint ebo_{ 0 };
		GLsizei index_count_{ 0 };
	};
}
