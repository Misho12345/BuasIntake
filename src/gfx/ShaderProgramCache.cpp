#include "pch.hpp"

#include "gfx/ShaderProgramCache.hpp"

namespace game::gfx
{
    ShaderProgramCache& ShaderProgramCache::instance()
    {
        static ShaderProgramCache cache;
        return cache;
    }

    Result<std::shared_ptr<ShaderProgram>> ShaderProgramCache::get_or_create(
        std::string key,
        const std::function<Result<GLuint>()>& build_program)
    {
        if (const auto it = programs_.find(key);
            it != programs_.end())
            return it->second;

        auto program_id = build_program();
        if (!program_id) return fail(program_id.error());

        auto program = std::make_shared<ShaderProgram>();
        program->id  = *program_id;

        programs_.emplace(std::move(key), program);
        return program;
    }

    void ShaderProgramCache::clear() { programs_.clear(); }
}
