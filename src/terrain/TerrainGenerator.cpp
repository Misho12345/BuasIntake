#include "pch.hpp"
#include "TerrainGenerator.hpp"

#include "gfx/ComputeDispatcher.hpp"
#include "terrain/TerrainGridMath.hpp"

namespace game::terrain
{
		namespace
	{
		struct GpuBoundaryEdge final
		{
			std::uint32_t a{};
			std::uint32_t b{};
		};

		struct GpuCounters final
		{
			std::uint32_t vertex_count{};
			std::uint32_t index_count{};
			std::uint32_t edge_count{};
		};

		constexpr GLuint image_binding = 0;
		constexpr GLuint boundary_vertices_binding = 1;
		constexpr GLuint horizontal_edge_ids_binding = 2;
		constexpr GLuint vertical_edge_ids_binding = 3;
		constexpr GLuint boundary_vertex_counter_binding = 4;
		constexpr GLuint mesh_vertices_binding = 4;
		constexpr GLuint mesh_indices_binding = 5;
		constexpr GLuint boundary_edges_binding = 6;
		constexpr GLuint counters_binding = 7;

		std::uint32_t generated_cell_count(const ChunkSettings& settings)
		{
			return (settings.field_size.x - 1u + settings.field_padding.x) *
				(settings.field_size.y - 1u + settings.field_padding.y);
		}

		std::uint32_t max_boundary_vertex_count(const ChunkSettings& settings)
		{
			const auto padded_size = padded_field_size(settings);
			const auto width = padded_size.x;
			const auto height = padded_size.y;
			return height * (width - 1u) + width * (height - 1u);
		}

		constexpr float terrain_iso = 0.0f;
		constexpr GLuint64 wait_timeout_ns = 1000000000ull;
	}

	TerrainGenerator::TerrainGenerator(const ChunkSettings& settings) :
		settings_{ settings },
		terrain_shader_{ gfx::Shader::from_compute_file("assets/shaders/terrain_gen.comp") },
		edge_shader_{ gfx::Shader::from_compute_file("assets/shaders/chunk_edges_gen.comp") },
		mesh_shader_{ gfx::Shader::from_compute_file("assets/shaders/chunk_mesh_gen.comp") },
		field_texture_{ padded_field_size(settings), gfx::TextureFormat::RGBA32F }
	{
		static_assert(sizeof(FieldSample) == sizeof(float) * 4);

		const auto padded_size = padded_field_size(settings_);
		boundary_vertices_buffer_.allocate_persistent_read<vec2>(max_boundary_vertex_count(settings_));
		horizontal_edge_ids_buffer_.resize<std::int32_t>(padded_size.y * (padded_size.x - 1u));
		vertical_edge_ids_buffer_.resize<std::int32_t>(padded_size.y * padded_size.x);
		boundary_vertex_counter_buffer_.allocate_persistent_read<std::uint32_t>(1);
		mesh_vertices_buffer_.allocate_persistent_read<vec2>(generated_cell_count(settings_) * 12u);
		mesh_indices_buffer_.allocate_persistent_read<std::uint32_t>(generated_cell_count(settings_) * 12u);
		boundary_edges_buffer_.allocate_persistent_read<GpuBoundaryEdge>(generated_cell_count(settings_) * 2u);
		counters_buffer_.allocate_persistent_read<GpuCounters>(1);
	}

	TerrainGenerator::~TerrainGenerator()
	{
		clear_completion_fence();
	}

	TerrainGenerator::TerrainGenerator(TerrainGenerator&& other) noexcept :
		settings_{ other.settings_ },
		terrain_shader_{ std::move(other.terrain_shader_) },
		edge_shader_{ std::move(other.edge_shader_) },
		mesh_shader_{ std::move(other.mesh_shader_) },
		field_texture_{ std::move(other.field_texture_) },
		boundary_vertices_buffer_{ std::move(other.boundary_vertices_buffer_) },
		horizontal_edge_ids_buffer_{ std::move(other.horizontal_edge_ids_buffer_) },
		vertical_edge_ids_buffer_{ std::move(other.vertical_edge_ids_buffer_) },
		boundary_vertex_counter_buffer_{ std::move(other.boundary_vertex_counter_buffer_) },
		mesh_vertices_buffer_{ std::move(other.mesh_vertices_buffer_) },
		mesh_indices_buffer_{ std::move(other.mesh_indices_buffer_) },
		boundary_edges_buffer_{ std::move(other.boundary_edges_buffer_) },
		counters_buffer_{ std::move(other.counters_buffer_) },
		completion_fence_{ std::exchange(other.completion_fence_, nullptr) },
		pending_readback_{ std::exchange(other.pending_readback_, false) } {}

	TerrainGenerator& TerrainGenerator::operator=(TerrainGenerator&& other) noexcept
	{
		if (this == &other) return *this;

		clear_completion_fence();

		settings_ = other.settings_;
		terrain_shader_ = std::move(other.terrain_shader_);
		edge_shader_ = std::move(other.edge_shader_);
		mesh_shader_ = std::move(other.mesh_shader_);
		field_texture_ = std::move(other.field_texture_);
		boundary_vertices_buffer_ = std::move(other.boundary_vertices_buffer_);
		horizontal_edge_ids_buffer_ = std::move(other.horizontal_edge_ids_buffer_);
		vertical_edge_ids_buffer_ = std::move(other.vertical_edge_ids_buffer_);
		boundary_vertex_counter_buffer_ = std::move(other.boundary_vertex_counter_buffer_);
		mesh_vertices_buffer_ = std::move(other.mesh_vertices_buffer_);
		mesh_indices_buffer_ = std::move(other.mesh_indices_buffer_);
		boundary_edges_buffer_ = std::move(other.boundary_edges_buffer_);
		counters_buffer_ = std::move(other.counters_buffer_);
		completion_fence_ = std::exchange(other.completion_fence_, nullptr);
		pending_readback_ = std::exchange(other.pending_readback_, false);
		return *this;
	}

	void TerrainGenerator::dispatch()
	{
		assert(!pending_readback_ && "TerrainGenerator dispatch called before previous readback");

		const auto layout = field_layout();
		const auto groups = gfx::ComputeDispatcher::groups_for(layout.padded_size, 16, 16);

		const std::array generation_passes
		{
			gfx::ComputeDispatcher::Pass{
				.shader = &terrain_shader_,
				.groups = groups,
				.configure = [this, layout](const gfx::Shader& shader)
				{
					bind_generation_pass(shader, layout);
				},
				.barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
			}
		};

		gfx::ComputeDispatcher::run(generation_passes);
		dispatch_surface_rebuild(terrain_channel_index, terrain_iso);
	}

	void TerrainGenerator::dispatch_surface_rebuild(const std::uint32_t channel_index, const float iso)
	{
		reset_surface_buffers();

		const auto layout = field_layout();
		const auto groups = gfx::ComputeDispatcher::groups_for(layout.padded_size, 16, 16);

		const std::array rebuild_passes
		{
			gfx::ComputeDispatcher::Pass{
				.shader = &edge_shader_,
				.groups = groups,
				.configure = [this, layout, channel_index, iso](const gfx::Shader& shader)
				{
					bind_surface_edge_pass(shader, layout, channel_index, iso);
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT
			},
			gfx::ComputeDispatcher::Pass{
				.shader = &mesh_shader_,
				.groups = groups,
				.configure = [this, layout, channel_index, iso](const gfx::Shader& shader)
				{
					bind_surface_mesh_pass(shader, layout, channel_index, iso);
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT
			}
		};

		gfx::ComputeDispatcher::run(rebuild_passes);
		replace_completion_fence();
	}

	void TerrainGenerator::upload_field(const std::span<const FieldSample> field_samples)
	{
		const auto size = field_texture_.size();
		const auto expected_count = static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y);
		assert(field_samples.size() == expected_count && "Uploaded field data size must match the texture extent");

		glTextureSubImage2D(
			field_texture_.native_handle(),
			0,
			0,
			0,
			static_cast<GLsizei>(size.x),
			static_cast<GLsizei>(size.y),
			GL_RGBA,
			GL_FLOAT,
			field_samples.empty() ? nullptr : field_samples.data());
	}

	std::vector<TerrainGenerator::FieldSample> TerrainGenerator::read_field() const
	{
		const auto size = field_texture_.size();
		std::vector<FieldSample> field_samples(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y));
		if (field_samples.empty()) return field_samples;

		glGetTextureImage(
			field_texture_.native_handle(),
			0,
			GL_RGBA,
			GL_FLOAT,
			static_cast<GLsizei>(field_samples.size() * sizeof(FieldSample)),
			field_samples.data());

		return field_samples;
	}

	TerrainGenerator::RawPipelineResult TerrainGenerator::readback()
	{
		if (!pending_readback_) return {};

		wait_for_completion();
		return consume_readback();
	}

	TerrainGenerator::FieldLayout TerrainGenerator::field_layout() const
	{
		return {
			.padded_size = padded_field_size(settings_),
			.cell_size = cell_size(settings_),
			.field_origin = field_origin(settings_),
			.field_padding = field_padding(settings_)
		};
	}

	void TerrainGenerator::bind_chunk_uniforms(const gfx::Shader& shader, const FieldLayout& layout) const
	{
		shader.set_uniform("uChunkCoord", settings_.chunk_coord);
		shader.set_uniform("uChunkGridSize", settings_.chunk_grid_size);
		shader.set_uniform("uChunkSize", settings_.chunk_size);
		shader.set_uniform("uWorldCenter", settings_.world_center);
		shader.set_uniform("uFieldOrigin", layout.field_origin);
		shader.set_uniform("uCellSize", layout.cell_size);
	}

	void TerrainGenerator::bind_generation_pass(const gfx::Shader& shader, const FieldLayout& layout) const
	{
		field_texture_.bind_image(image_binding, GL_WRITE_ONLY);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uSeed", settings_.seed);
		shader.set_uniform("uPlanetRadius", settings_.planet_radius);
	}

	void TerrainGenerator::bind_surface_edge_pass(
		const gfx::Shader& shader,
		const FieldLayout& layout,
		const std::uint32_t channel_index,
		const float iso) const
	{
		field_texture_.bind_image(image_binding, GL_READ_ONLY);
		boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
		horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
		vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
		boundary_vertex_counter_buffer_.bind_base(boundary_vertex_counter_binding);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uIso", iso);
		shader.set_uniform("uChannelIndex", static_cast<std::int32_t>(channel_index));
	}

	void TerrainGenerator::bind_surface_mesh_pass(
		const gfx::Shader& shader,
		const FieldLayout& layout,
		const std::uint32_t channel_index,
		const float iso) const
	{
		field_texture_.bind_image(image_binding, GL_READ_ONLY);
		boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
		horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
		vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
		mesh_vertices_buffer_.bind_base(mesh_vertices_binding);
		mesh_indices_buffer_.bind_base(mesh_indices_binding);
		boundary_edges_buffer_.bind_base(boundary_edges_binding);
		counters_buffer_.bind_base(counters_binding);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uIso", iso);
		shader.set_uniform("uChannelIndex", static_cast<std::int32_t>(channel_index));
		shader.set_uniform("uFieldPadding", layout.field_padding);
		shader.set_uniform("uFieldSize", ivec2{
			static_cast<std::int32_t>(settings_.field_size.x),
			static_cast<std::int32_t>(settings_.field_size.y)
		});
	}

	void TerrainGenerator::reset_surface_buffers()
	{
		static constexpr std::int32_t minus_one = -1;
		static constexpr std::uint32_t zero = 0;

		glClearNamedBufferData(horizontal_edge_ids_buffer_.id(), GL_R32I, GL_RED_INTEGER, GL_INT, &minus_one);
		glClearNamedBufferData(vertical_edge_ids_buffer_.id(), GL_R32I, GL_RED_INTEGER, GL_INT, &minus_one);
		glClearNamedBufferData(boundary_vertex_counter_buffer_.id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
		glClearNamedBufferData(counters_buffer_.id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
	}

	void TerrainGenerator::replace_completion_fence()
	{
		clear_completion_fence();
		completion_fence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		assert(completion_fence_ != nullptr && "Failed to create terrain generator fence");
		pending_readback_ = true;
	}

	void TerrainGenerator::clear_completion_fence()
	{
		if (completion_fence_ != nullptr)
		{
			glDeleteSync(completion_fence_);
			completion_fence_ = nullptr;
		}
	}

	void TerrainGenerator::wait_for_completion()
	{
		if (!pending_readback_ || completion_fence_ == nullptr) return;

		for (;;)
		{
			const auto wait_result = glClientWaitSync(completion_fence_, GL_SYNC_FLUSH_COMMANDS_BIT, wait_timeout_ns);
			if (wait_result == GL_ALREADY_SIGNALED || wait_result == GL_CONDITION_SATISFIED)
			{
				return;
			}

			if (wait_result == GL_WAIT_FAILED)
			{
				throw std::runtime_error("TerrainGenerator failed while waiting for GPU completion");
			}
		}
	}

	TerrainGenerator::RawPipelineResult TerrainGenerator::consume_readback()
	{
		RawPipelineResult result{};
		if (!pending_readback_) return result;

		glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

		const auto boundary_vertex_count = boundary_vertex_counter_buffer_.read_one<std::uint32_t>();
		const auto [
			vertex_count,
			index_count,
			edge_count
		] = counters_buffer_.read_one<GpuCounters>();

		clear_completion_fence();
		pending_readback_ = false;

		if (boundary_vertex_count > 0)
		{
			const auto gpu_boundary_vertices = boundary_vertices_buffer_.read<vec2>(boundary_vertex_count);
			result.boundary_vertices.reserve(gpu_boundary_vertices.size());
			for (const auto& vertex : gpu_boundary_vertices)
			{
				result.boundary_vertices.push_back(vertex);
			}
		}

		if (vertex_count > 0)
		{
			const auto gpu_mesh_vertices = mesh_vertices_buffer_.read<vec2>(vertex_count);
			result.mesh_vertices.reserve(gpu_mesh_vertices.size());
			for (const auto& vertex : gpu_mesh_vertices)
			{
				result.mesh_vertices.push_back(vertex);
			}
		}

		if (index_count > 0)
		{
			result.mesh_indices = mesh_indices_buffer_.read<std::uint32_t>(index_count);
		}

		if (edge_count > 0)
		{
			const auto gpu_boundary_edges = boundary_edges_buffer_.read<GpuBoundaryEdge>(edge_count);
			result.boundary_edges.reserve(gpu_boundary_edges.size());
			for (const auto& [a, b] : gpu_boundary_edges)
			{
				result.boundary_edges.emplace_back(a, b);
			}
		}

		return result;
	}
}
