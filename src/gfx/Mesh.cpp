#include "pch.hpp"

#include "Mesh.hpp"

namespace game::gfx
{
    Mesh::Mesh()
    {
        // the vao remembers how sf::Vertex is laid out, while the vbo/ebo only hold the latest uploaded mesh data
        // this is set up once because all generated meshes in this project use the same position/color/texcoord format
        vao_.create();
        vbo_.create();
        ebo_.create();

        glVertexArrayVertexBuffer(vao_.id(), 0, vbo_.id(), 0, sizeof(sf::Vertex));
        glVertexArrayElementBuffer(vao_.id(), ebo_.id());

        // layout(location = 0) in vec2 aPosition;
        static constexpr GLint pos_size = sizeof(sf::Vertex::position) / sizeof(float); // x2 float
        glEnableVertexArrayAttrib(vao_.id(), 0);
        glVertexArrayAttribFormat(vao_.id(), 0, pos_size, GL_FLOAT, GL_FALSE, offsetof(sf::Vertex, position));
        glVertexArrayAttribBinding(vao_.id(), 0, 0);

        // layout(location = 1) in vec4 aColor;
        static constexpr GLint color_size = sizeof(sf::Vertex::color) / sizeof(std::uint8_t); // x4 uint8
        glEnableVertexArrayAttrib(vao_.id(), 1);
        glVertexArrayAttribFormat(vao_.id(), 1, color_size, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(sf::Vertex, color));
        glVertexArrayAttribBinding(vao_.id(), 1, 0);

        // layout(location = 2) in vec2 aTexCoords;
        static constexpr GLint tex_coords_size = sizeof(sf::Vertex::texCoords) / sizeof(float); // x2 float
        glEnableVertexArrayAttrib(vao_.id(), 2);
        glVertexArrayAttribFormat(vao_.id(), 2, tex_coords_size, GL_FLOAT, GL_FALSE, offsetof(sf::Vertex, texCoords));
        glVertexArrayAttribBinding(vao_.id(), 2, 0);
    }

    Mesh::Mesh(Mesh&& other) noexcept
        : vao_{ std::move(other.vao_) },
          vbo_{ std::move(other.vbo_) },
          ebo_{ std::move(other.ebo_) },
          index_count_{ std::exchange(other.index_count_, 0) } {}

    Mesh& Mesh::operator=(Mesh&& other) noexcept
    {
        if (this == &other) return *this;

        vao_         = std::move(other.vao_);
        vbo_         = std::move(other.vbo_);
        ebo_         = std::move(other.ebo_);
        index_count_ = std::exchange(other.index_count_, 0);

        return *this;
    }

    void Mesh::set_data(const std::span<const sf::Vertex> vertices, const std::span<const std::uint32_t> indices)
    {
        index_count_ = static_cast<GLsizei>(indices.size());

        // generated terrain and water rebuild whole chunks, so replacing the buffer contents is simpler than partial updates
        glNamedBufferData(
            vbo_.id(),
            static_cast<GLsizeiptr>(vertices.size_bytes()),
            vertices.empty() ? nullptr : vertices.data(),
            GL_STATIC_DRAW);

        glNamedBufferData(
            ebo_.id(),
            static_cast<GLsizeiptr>(indices.size_bytes()),
            indices.empty() ? nullptr : indices.data(),
            GL_STATIC_DRAW);
    }

    void Mesh::draw(const GLenum primitive) const
    {
        if (empty()) return;

        // Mesh only knows how to draw indexed triangles, the active shader and textures are owned by the caller
        glBindVertexArray(vao_.id());
        glDrawElements(primitive, index_count_, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    bool Mesh::empty() const { return index_count_ == 0; }
}
