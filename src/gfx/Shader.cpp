#include "pch.hpp"
#include "Shader.hpp"

namespace game::gfx
{
	struct ShaderProgram final
	{
		GLuint id{ 0 };
		mutable std::unordered_map<std::string, GLint> uniform_locations{};

		~ShaderProgram()
		{
			if (id != 0) glDeleteProgram(id);
		}
	};

	namespace
	{
		std::string trim_log(std::string log)
		{
			while (!log.empty() && (log.back() == '\0' || log.back() == '\n' || log.back() == '\r'))
			{
				log.pop_back();
			}

			return log;
		}

		std::string shader_log(const GLuint shader)
		{
			GLint length = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

			if (length <= 1) return {};

			std::string log(static_cast<std::size_t>(length), '\0');
			glGetShaderInfoLog(shader, length, nullptr, log.data());
			return trim_log(std::move(log));
		}

		std::string program_log(const GLuint program)
		{
			GLint length = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

			if (length <= 1) return {};

			std::string log(static_cast<std::size_t>(length), '\0');
			glGetProgramInfoLog(program, length, nullptr, log.data());
			return trim_log(std::move(log));
		}

		std::string read_text_file(const fs::path& path)
		{
			const std::ifstream file{ path };
			if (!file)
			{
				std::println(std::cerr, "Failed to read {}", path.string());
				return {};
			}

			std::ostringstream stream;
			stream << file.rdbuf();
			return stream.str();
		}

		auto& shader_cache()
		{
			static std::unordered_map<std::string, std::shared_ptr<ShaderProgram>> cache;
			return cache;
		}

		std::string normalized_key(const fs::path& path)
		{
			return path.lexically_normal().generic_string();
		}

		std::shared_ptr<ShaderProgram> get_or_create_cached_program(const std::string& key, auto&& builder)
		{
			auto& cache = shader_cache();
			if (const auto it = cache.find(key); it != cache.end())
			{
				return it->second;
			}

			const auto program_id = builder();
			if (program_id == 0) return {};

			auto program = std::make_shared<ShaderProgram>();
			program->id = program_id;
			cache.emplace(key, program);
			return program;
		}
	}

	Shader::Shader(std::shared_ptr<ShaderProgram> program) : program_{ std::move(program) } {}


	Shader Shader::from_compute_file(const fs::path& path)
	{
		const auto program = get_or_create_cached_program(
			std::format("compute:{}", normalized_key(path)),
			[&]()
			{
				const auto source = read_text_file(path);
				const auto stage = compile_stage(GL_COMPUTE_SHADER, source, path);
				if (stage == 0) return GLuint{ 0 };

				const std::array stages{ stage };
				const auto linked_program = link_program(stages, path.string());
				glDeleteShader(stage);
				return linked_program;
			});

		return Shader{ program };
	}

	Shader Shader::from_graphics_files(const fs::path& vertex_path, const fs::path& fragment_path)
	{
		const auto program = get_or_create_cached_program(
			std::format("graphics:{}|{}", normalized_key(vertex_path), normalized_key(fragment_path)),
			[&]()
			{
				const auto vertex_source = read_text_file(vertex_path);
				const auto fragment_source = read_text_file(fragment_path);

				const auto vertex_stage = compile_stage(GL_VERTEX_SHADER, vertex_source, vertex_path);
				if (vertex_stage == 0) return GLuint{ 0 };

				const auto fragment_stage = compile_stage(GL_FRAGMENT_SHADER, fragment_source, fragment_path);
				if (fragment_stage == 0)
				{
					glDeleteShader(vertex_stage);
					return GLuint{ 0 };
				}

				const std::array stages{ vertex_stage, fragment_stage };
				const auto linked_program = link_program(stages, std::format("{} + {}", vertex_path.string(), fragment_path.string()));

				glDeleteShader(vertex_stage);
				glDeleteShader(fragment_stage);
				return linked_program;
			});

		return Shader{ program };
	}

	void Shader::clear_cache()
	{
		shader_cache().clear();
	}


	void Shader::use() const
	{
		assert(program_ && program_->id != 0 && "Shader program is not initialized");
		glUseProgram(program_->id);
	}

	GLuint Shader::id() const { return program_ ? program_->id : 0; }
	bool   Shader::valid() const { return program_ && program_->id != 0; }


	GLuint Shader::compile_stage(const GLenum stage, const std::string& source, const fs::path& path)
	{
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
			std::println(std::cerr,
			             "Failed to compile shader '{}':\n{}",
			             path.string(),
			             log.empty() ? "No additional info" : log);
			return 0;
		}

		return shader;
	}

	GLuint Shader::link_program(const std::span<const GLuint> shaders, const std::string_view label)
	{
		const GLuint program = glCreateProgram();

		for (const auto shader : shaders)
		{
			glAttachShader(program, shader);
		}

		glLinkProgram(program);

		for (const auto shader : shaders)
		{
			glDetachShader(program, shader);
		}

		GLint linked = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);

		if (linked == GL_FALSE)
		{
			const auto log = program_log(program);
			glDeleteProgram(program);
			std::println(std::cerr,
			             "Failed to link shader program '{}':\n{}",
			             label,
			             log.empty() ? "No additional info" : log);
			return 0;
		}

		return program;
	}

	GLint Shader::uniform_location(const std::string_view name) const
	{
		assert(program_ && program_->id != 0 && "Shader program is not initialized");

		const std::string key{ name };
		if (const auto it = program_->uniform_locations.find(key); it != program_->uniform_locations.end())
		{
			return it->second;
		}

		const auto location = glGetUniformLocation(program_->id, key.c_str());
		program_->uniform_locations.emplace(key, location);
		return location;
	}
}
