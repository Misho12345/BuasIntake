#include "pch.hpp"
#include "Mesh.hpp"

namespace game::gfx
{
	Mesh::Mesh()
	{
		glCreateVertexArrays(1, &vao_);
		glCreateBuffers(1, &vbo_);
		glCreateBuffers(1, &ebo_);

		glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(sf::Vertex));
		glVertexArrayElementBuffer(vao_, ebo_);

		static constexpr GLint pos_size = 2;
		glEnableVertexArrayAttrib(vao_, 0);
		glVertexArrayAttribFormat(vao_, 0, pos_size, GL_FLOAT, GL_FALSE, offsetof(sf::Vertex, position));
		glVertexArrayAttribBinding(vao_, 0, 0);

		static constexpr GLint color_size = 4;
		glEnableVertexArrayAttrib(vao_, 1);
		glVertexArrayAttribFormat(vao_, 1, color_size, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(sf::Vertex, color));
		glVertexArrayAttribBinding(vao_, 1, 0);

		static constexpr GLint tex_coords_size = 2;
		glEnableVertexArrayAttrib(vao_, 2);
		glVertexArrayAttribFormat(vao_, 2, tex_coords_size, GL_FLOAT, GL_FALSE, offsetof(sf::Vertex, texCoords));
		glVertexArrayAttribBinding(vao_, 2, 0);
	}

	Mesh::~Mesh()
	{
		if (ebo_ != 0) glDeleteBuffers(1, &ebo_);
		if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
		if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
	}

	Mesh::Mesh(Mesh&& other) noexcept :
		vao_{ std::exchange(other.vao_, 0) },
		vbo_{ std::exchange(other.vbo_, 0) },
		ebo_{ std::exchange(other.ebo_, 0) },
		index_count_{ std::exchange(other.index_count_, 0) } {}

	Mesh& Mesh::operator=(Mesh&& other) noexcept
	{
		if (this == &other) return *this;

		if (ebo_ != 0) glDeleteBuffers(1, &ebo_);
		if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
		if (vao_ != 0) glDeleteVertexArrays(1, &vao_);

		vao_         = std::exchange(other.vao_, 0);
		vbo_         = std::exchange(other.vbo_, 0);
		ebo_         = std::exchange(other.ebo_, 0);
		index_count_ = std::exchange(other.index_count_, 0);
		return *this;
	}

	void Mesh::set_data(const std::span<const sf::Vertex> vertices,
		const std::span<const std::uint32_t> indices)
	{
		index_count_ = static_cast<GLsizei>(indices.size());

		glNamedBufferData(
			vbo_,
			static_cast<GLsizeiptr>(vertices.size_bytes()),
			vertices.empty() ? nullptr : vertices.data(),
			GL_STATIC_DRAW);

		glNamedBufferData(
			ebo_,
			static_cast<GLsizeiptr>(indices.size_bytes()),
			indices.empty() ? nullptr : indices.data(),
			GL_STATIC_DRAW);
	}

	void Mesh::draw() const
	{
		if (empty()) return;

		glBindVertexArray(vao_);
		glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
		glBindVertexArray(0);
	}

	bool Mesh::empty() const { return index_count_ == 0; }
}
