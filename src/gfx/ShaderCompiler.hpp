#pragma once

#include "pch.hpp"

namespace game::gfx
{
    // low-level shader compile/link wrapper used by Shader
    class ShaderCompiler final
    {
    public:
        static Result<GLuint> compile_stage(GLenum stage, const std::string& source, const fs::path& path);
        static Result<GLuint> link_program(std::span<const GLuint> shaders, std::string_view label);

    private:
        // helpers for getting the logs on compilation & link fail

        static std::string shader_log(GLuint shader);
        static std::string program_log(GLuint program);
        static std::string trim_log(std::string log);
    };
}
