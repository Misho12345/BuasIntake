#pragma once

#include "pch.hpp"

namespace game::gfx
{
    struct ShaderProgram;

    // lightweight handle to a cached compute or graphics shader program
    class Shader final
    {
    public:
        Shader()  = default;
        ~Shader() = default;

        Shader(const Shader&)                      = delete;
        Shader& operator=(const Shader&)           = delete;
        Shader(Shader&& other) noexcept            = default;
        Shader& operator=(Shader&& other) noexcept = default;

        static Result<Shader> from_compute_file(const fs::path& path);

        static Result<Shader> from_graphics_files(const fs::path& vertex_path, const fs::path& fragment_path);

        static void clear_cache();

        Result<void> use() const;
        bool         valid() const;

        template <typename T>
        void set_uniform(const std::string_view name, const T& value) const
        {
            if (const auto location = uniform_location(name); location >= 0)
            {
                if constexpr (std::same_as<T, float>) glUniform1f(location, value);
                else if constexpr (std::same_as<T, vec2>) glUniform2f(location, value.x, value.y);
                else if constexpr (std::same_as<T, vec3>) glUniform3f(location, value.x, value.y, value.z);
                else if constexpr (std::same_as<T, vec4>) glUniform4f(location, value.x, value.y, value.z, value.w);

                else if constexpr (std::same_as<T, std::int32_t>) glUniform1i(location, value);
                else if constexpr (std::same_as<T, ivec2>) glUniform2i(location, value.x, value.y);
                else if constexpr (std::same_as<T, ivec3>) glUniform3i(location, value.x, value.y, value.z);
                else if constexpr (std::same_as<T, ivec4>) glUniform4i(location, value.x, value.y, value.z, value.w);

                else if constexpr (std::same_as<T, std::uint32_t>) glUniform1ui(location, value);
                else if constexpr (std::same_as<T, uvec2>) glUniform2ui(location, value.x, value.y);
                else if constexpr (std::same_as<T, uvec3>) glUniform3ui(location, value.x, value.y, value.z);
                else if constexpr (std::same_as<T, uvec4>) glUniform4ui(location, value.x, value.y, value.z, value.w);

                else if constexpr (std::same_as<T, mat3>) glUniformMatrix3fv(location, 1, GL_FALSE, value.array.data());
                else if constexpr (std::same_as<T, mat4>) glUniformMatrix4fv(location, 1, GL_FALSE, value.array.data());

                else static_assert(false, "Unsupported uniform type");;
            }
        }

    private:
        explicit Shader(std::shared_ptr<ShaderProgram> program);

        GLint uniform_location(std::string_view name) const;

        std::shared_ptr<ShaderProgram> program_{};
    };
}
