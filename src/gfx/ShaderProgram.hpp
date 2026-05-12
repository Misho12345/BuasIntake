#pragma once

#include "pch.hpp"

namespace game::gfx
{
    // contains the handle for a compute or graphics shader program
    // owned by ShaderProgramCache and saved in Shader as an std::shared_ptr
    // it caches the uniform locations in Shader::uniform_location(...)
    struct ShaderProgram final
    {
        GLuint id{ 0 };
        mutable std::unordered_map<std::string, GLint> uniform_locations{};

        ~ShaderProgram()
        {
            if (id != 0) glDeleteProgram(id);
        }
    };
}
