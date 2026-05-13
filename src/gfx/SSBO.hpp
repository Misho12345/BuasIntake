#pragma once

#include "pch.hpp"


namespace game::gfx
{
    // shader storage buffer object wrapper
    // supports both mutable (glNamedBufferData) and immutable (glNamedBufferStorage) storage;
    // immutable storage is used for persistent mapping - once allocated it cannot be resized
    class SSBO final
    {
    public:
        explicit SSBO(const GLenum usage = GL_DYNAMIC_COPY) : usage_{ usage }
        {
            glCreateBuffers(1, &id_);
        }

        ~SSBO()
        {
            unmap_if_needed();
            if (id_ != 0) glDeleteBuffers(1, &id_);
        }

        SSBO(const SSBO&)            = delete;
        SSBO& operator=(const SSBO&) = delete;

        SSBO(SSBO&& other) noexcept
            : id_{ std::exchange(other.id_, 0) },
              usage_{ other.usage_ },
              size_bytes_{ std::exchange(other.size_bytes_, 0) },
              mapped_ptr_{ std::exchange(other.mapped_ptr_, nullptr) },
              immutable_storage_{ std::exchange(other.immutable_storage_, false) } {}

        SSBO& operator=(SSBO&& other) noexcept
        {
            if (this == &other) return *this;
            unmap_if_needed();
            if (id_ != 0) glDeleteBuffers(1, &id_);

            id_                = std::exchange(other.id_, 0);
            usage_             = other.usage_;
            size_bytes_        = std::exchange(other.size_bytes_, 0);
            mapped_ptr_        = std::exchange(other.mapped_ptr_, nullptr);
            immutable_storage_ = std::exchange(other.immutable_storage_, false);

            return *this;
        }

        // resizes mutable storage; will log an error and no-op if called on immutable storage
        template <typename T> requires std::is_trivially_copyable_v<T>
        void resize(const std::size_t count) { resize_bytes(sizeof(T) * count); }

        // allocates immutable, persistently mapped, coherent read storage
        // use when the CPU needs continuous read access without explicit sync (e.g. GPU-written readback buffers)
        template <typename T> requires std::is_trivially_copyable_v<T>
        void allocate_persistent_read(const std::size_t count)
        {
            const auto byte_count = sizeof(T) * count;
            allocate_storage_bytes(
                byte_count,
                GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT |
                GL_DYNAMIC_STORAGE_BIT,
                GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
        }

        // reads count elements starting at offset_elements from the buffer
        // if the buffer is persistently mapped it reads from the mapped pointer directly, otherwise uses glGetNamedBufferSubData
        template <typename T> requires std::is_trivially_copyable_v<T>
        std::vector<T> read(const std::size_t count, const std::size_t offset_elements = 0) const
        {
            std::vector<T> output(count);
            if (count == 0) return output;

            const auto offset = offset_elements * sizeof(T);
            if (offset + sizeof(T) * count > size_bytes_)
            {
                Log::error("SSBO read exceeds buffer size: offset={}, bytes={}, capacity={}",
                           offset, sizeof(T) * count, size_bytes_);
                return {};
            }

            if (mapped_ptr_ != nullptr)
            {
                std::memcpy(output.data(), static_cast<const std::byte*>(mapped_ptr_) + offset, sizeof(T) * count);
            }
            else
            {
                glGetNamedBufferSubData(
                    id_,
                    static_cast<GLintptr>(offset),
                    static_cast<GLsizeiptr>(sizeof(T) * count),
                    output.data());
            }

            return output;
        }

        // single-element variant of read(...)
        template <typename T> requires std::is_trivially_copyable_v<T>
        T read_one(const std::size_t offset_elements = 0) const
        {
            T          output{};
            const auto offset = offset_elements * sizeof(T);
            if (offset + sizeof(T) > size_bytes_)
            {
                Log::error("SSBO single-value read exceeds buffer size: offset={}, bytes={}, capacity={}",
                           offset, sizeof(T), size_bytes_);
                return output;
            }

            if (mapped_ptr_ != nullptr)
            {
                std::memcpy(&output, static_cast<const std::byte*>(mapped_ptr_) + offset, sizeof(T));
            }
            else
            {
                glGetNamedBufferSubData(
                    id_,
                    static_cast<GLintptr>(offset),
                    static_cast<GLsizeiptr>(sizeof(T)),
                    &output);
            }

            return output;
        }

        void bind_base(const GLuint binding) const
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id_);
        }

        GLuint id() const { return id_; }

    private:
        void unmap_if_needed()
        {
            if (id_ != 0 && mapped_ptr_ != nullptr)
            {
                glUnmapNamedBuffer(id_);
                mapped_ptr_ = nullptr;
            }
        }

        void allocate_storage_bytes(const std::size_t byte_count, const GLbitfield storage_flags,
                                    const GLbitfield  map_flags)
        {
            unmap_if_needed();
            size_bytes_        = byte_count;
            immutable_storage_ = true;

            // immutable storage is required for persistent mapping, so resize(...) is intentionally disabled afterwards
            glNamedBufferStorage(id_, static_cast<GLsizeiptr>(byte_count), nullptr, storage_flags);

            // immediately map the entire buffer so callers can read without another map call later
            mapped_ptr_ = byte_count == 0
                              ? nullptr
                              : glMapNamedBufferRange(
                                  id_, 0,
                                  static_cast<GLsizeiptr>(byte_count),
                                  map_flags);
        }

        void resize_bytes(const std::size_t byte_count)
        {
            if (immutable_storage_)
            {
                Log::error("Cannot resize immutable SSBO storage");
                return;
            }

            size_bytes_ = byte_count;
            glNamedBufferData(id_, static_cast<GLsizeiptr>(byte_count), nullptr, usage_);
        }

        GLuint      id_{ 0 };
        GLenum      usage_{ GL_DYNAMIC_COPY };
        std::size_t size_bytes_{ 0 };
        void*       mapped_ptr_{ nullptr }; // non-null only for persistently mapped immutable storage
        bool        immutable_storage_{ false };
    };
}
