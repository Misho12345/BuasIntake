#include "pch.hpp"

#include "gfx/ShaderCompiler.hpp"

namespace game::gfx
{
    Result<GLuint> ShaderCompiler::compile_stage(const GLenum stage, const std::string& source, const fs::path& path)
    {
        if (source.empty()) return fail("Shader source for '{}' is empty", path.string());

        const GLuint shader = glCreateShader(stage);
        const char*  data   = source.c_str();
        glShaderSource(shader, 1, &data, nullptr);
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

        if (compiled == GL_FALSE)
        {
            const auto log = shader_log(shader);
            glDeleteShader(shader);
            return fail("Failed to compile shader '{}': {}", path.string(), log.empty() ? "No additional info" : log);
        }

        return shader;
    }

    Result<GLuint> ShaderCompiler::link_program(const std::span<const GLuint> shaders, const std::string_view label)
    {
        if (shaders.empty()) return fail("No shader stages provided for program '{}'", label);

        const GLuint program = glCreateProgram();

        for (const auto shader : shaders) glAttachShader(program, shader);

        glLinkProgram(program);

        for (const auto shader : shaders) glDetachShader(program, shader);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);

        if (linked == GL_FALSE)
        {
            const auto log = program_log(program);
            glDeleteProgram(program);
            return fail("Failed to link shader program '{}': {}", label, log.empty() ? "No additional info" : log);
        }

        return program;
    }

    std::string ShaderCompiler::shader_log(const GLuint shader)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        if (length <= 1) return {};

        std::string log(static_cast<std::size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        return trim_log(std::move(log));
    }

    std::string ShaderCompiler::program_log(const GLuint program)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        if (length <= 1) return {};

        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        return trim_log(std::move(log));
    }

    std::string ShaderCompiler::trim_log(std::string log)
    {
        while (!log.empty() && (log.back() == '\0' || log.back() == '\n' || log.back() == '\r')) log.pop_back();
        return log;
    }
}
