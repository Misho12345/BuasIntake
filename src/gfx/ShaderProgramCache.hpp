#pragma once

#include "pch.hpp"

#include "gfx/ShaderProgram.hpp"

namespace game::gfx
{
    // saves shader programs so that they wouldn't have to be compiled and linked repeatedly
    class ShaderProgramCache final
    {
    public:
        static ShaderProgramCache& instance();

        Result<std::shared_ptr<ShaderProgram>> get_or_create(
            std::string key,
            const std::function<Result<GLuint>()>& build_program);

        void clear();

    private:
        ShaderProgramCache() = default;

        std::unordered_map<
            std::string,
            std::shared_ptr<ShaderProgram>
        > programs_{};
    };
}
