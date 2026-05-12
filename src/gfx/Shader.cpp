#include "pch.hpp"

#include "Shader.hpp"

#include "gfx/ShaderCompiler.hpp"
#include "gfx/ShaderPreprocessor.hpp"
#include "gfx/ShaderProgram.hpp"
#include "gfx/ShaderProgramCache.hpp"

namespace game::gfx
{
    namespace
    {
        std::string normalized_key(const fs::path& path) { return path.lexically_normal().generic_string(); }
    }

    Shader::Shader(std::shared_ptr<ShaderProgram> program) : program_{ std::move(program) } {}

    Result<Shader> Shader::from_compute_file(const fs::path& path)
    {
        const auto program = ShaderProgramCache::instance().get_or_create(
            std::format("compute:{}", normalized_key(path)),
            [&]() -> Result<GLuint>
            {
                auto source = ShaderPreprocessor::preprocess_file(path);
                if (!source) return fail("{}", source.error().message);

                auto stage = ShaderCompiler::compile_stage(GL_COMPUTE_SHADER, *source, path);
                if (!stage) return fail("{}", stage.error().message);

                const std::array stages{ *stage };
                auto             linked_program = ShaderCompiler::link_program(stages, path.string());
                glDeleteShader(*stage);
                return linked_program;
            });

        if (!program) return fail(program.error());
        return Shader{ *program };
    }

    Result<Shader> Shader::from_graphics_files(const fs::path& vertex_path, const fs::path& fragment_path)
    {
        const auto program = ShaderProgramCache::instance().get_or_create(
            std::format("graphics:{}|{}", normalized_key(vertex_path), normalized_key(fragment_path)),
            [&]() -> Result<GLuint>
            {
                auto vertex_source = ShaderPreprocessor::preprocess_file(vertex_path);
                if (!vertex_source) return fail("{}", vertex_source.error().message);

                auto fragment_source = ShaderPreprocessor::preprocess_file(fragment_path);
                if (!fragment_source) return fail("{}", fragment_source.error().message);

                auto vertex_stage = ShaderCompiler::compile_stage(GL_VERTEX_SHADER, *vertex_source, vertex_path);
                if (!vertex_stage) return fail("{}", vertex_stage.error().message);

                auto fragment_stage = ShaderCompiler::compile_stage(GL_FRAGMENT_SHADER, *fragment_source, fragment_path);
                if (!fragment_stage)
                {
                    glDeleteShader(*vertex_stage);
                    return fail("{}", fragment_stage.error().message);
                }

                const std::array stages{ *vertex_stage, *fragment_stage };
                auto linked_program = ShaderCompiler::link_program(
                    stages,
                    std::format("{} + {}", vertex_path.string(), fragment_path.string()));

                glDeleteShader(*vertex_stage);
                glDeleteShader(*fragment_stage);
                return linked_program;
            });

        if (!program) return fail(program.error());
        return Shader{ *program };
    }

    void Shader::clear_cache() { ShaderProgramCache::instance().clear(); }

    Result<void> Shader::use() const
    {
        if (!program_ || program_->id == 0) return fail("Shader program is not initialized");

        glUseProgram(program_->id);
        return {};
    }

    GLuint Shader::id() const { return program_ ? program_->id : 0; }
    bool   Shader::valid() const { return program_ && program_->id != 0; }

    GLint Shader::uniform_location(const std::string_view name) const
    {
        if (!program_ || program_->id == 0) return -1;

        const std::string key{ name };
        if (const auto it = program_->uniform_locations.find(key);
            it != program_->uniform_locations.end())
            return it->second;

        const auto location = glGetUniformLocation(program_->id, key.c_str());
        program_->uniform_locations.emplace(key, location);
        return location;
    }
}
