#include "pch.hpp"
#include "TerrainGenerator.hpp"

#include "gfx/ComputeDispatcher.hpp"

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


		vec2 cell_size(const ChunkSettings& settings)
		{
			return {
				settings.chunk_size.x / static_cast<float>(std::max(settings.field_size.x - 1u, 1u)),
				settings.chunk_size.y / static_cast<float>(std::max(settings.field_size.y - 1u, 1u))
			};
		}

		std::uint32_t chunk_cell_count(const ChunkSettings& settings)
		{
			return (settings.field_size.x - 1u) * (settings.field_size.y - 1u);
		}

		std::uint32_t max_boundary_vertex_count(const ChunkSettings& settings)
		{
			const auto width = settings.field_size.x;
			const auto height = settings.field_size.y;
			return height * (width - 1u) + width * (height - 1u);
		}

		constexpr float terrain_iso = 0.0f;
	}

	TerrainGenerator::TerrainGenerator(const ChunkSettings& settings) :
		settings_{ settings },
		terrain_shader_{ gfx::Shader::from_compute_file("assets/shaders/terrain_gen.comp") },
		edge_shader_{ gfx::Shader::from_compute_file("assets/shaders/chunk_edges_gen.comp") },
		mesh_shader_{ gfx::Shader::from_compute_file("assets/shaders/chunk_mesh_gen.comp") },
		field_texture_{ settings.field_size, gfx::TextureFormat::RGBA32F }
	{
		boundary_vertices_buffer_.resize<vec2>(max_boundary_vertex_count(settings_));
		horizontal_edge_ids_buffer_.resize<std::int32_t>(settings_.field_size.y * (settings_.field_size.x - 1u));
		vertical_edge_ids_buffer_.resize<std::int32_t>(settings_.field_size.y * settings_.field_size.x);
		boundary_vertex_counter_buffer_.resize<std::uint32_t>(1);
		mesh_vertices_buffer_.resize<vec2>(chunk_cell_count(settings_) * 12u);
		mesh_indices_buffer_.resize<std::uint32_t>(chunk_cell_count(settings_) * 12u);
		boundary_edges_buffer_.resize<GpuBoundaryEdge>(chunk_cell_count(settings_) * 2u);
		counters_buffer_.resize<GpuCounters>(1);
	}

	void TerrainGenerator::dispatch()
	{
		assert(!pending_readback_ && "TerrainGenerator dispatch called before previous readback");

		reset_buffers();

		const auto groups = gfx::ComputeDispatcher::groups_for(settings_.field_size, 16, 16);
		const auto terrain_cell_size = cell_size(settings_);

		const std::array passes
		{
			gfx::ComputeDispatcher::Pass{
				.shader = &terrain_shader_,
				.groups = groups,
				.configure = [this](const gfx::Shader& shader)
				{
					field_texture_.bind_image(image_binding, GL_WRITE_ONLY);
					shader.set_uniform("uChunkCoord", settings_.chunk_coord);
					shader.set_uniform("uChunkGridSize", settings_.chunk_grid_size);
					shader.set_uniform("uChunkSize", settings_.chunk_size);
					shader.set_uniform("uWorldCenter", settings_.world_center);
					shader.set_uniform("uSeed", settings_.seed);
					shader.set_uniform("uPlanetRadius", settings_.planet_radius);
				},
				.barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
			},
			gfx::ComputeDispatcher::Pass{
				.shader = &edge_shader_,
				.groups = groups,
				.configure = [this, terrain_cell_size](const gfx::Shader& shader)
				{
					field_texture_.bind_image(image_binding, GL_READ_ONLY);
					boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
					horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
					vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
					boundary_vertex_counter_buffer_.bind_base(boundary_vertex_counter_binding);
					shader.set_uniform("uIso", terrain_iso);
					shader.set_uniform("uChunkCoord", settings_.chunk_coord);
					shader.set_uniform("uChunkGridSize", settings_.chunk_grid_size);
					shader.set_uniform("uChunkSize", settings_.chunk_size);
					shader.set_uniform("uWorldCenter", settings_.world_center);
					shader.set_uniform("uCellSize", terrain_cell_size);
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT
			},
			gfx::ComputeDispatcher::Pass{
				.shader = &mesh_shader_,
				.groups = groups,
				.configure = [this, terrain_cell_size](const gfx::Shader& shader)
				{
					field_texture_.bind_image(image_binding, GL_READ_ONLY);
					boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
					horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
					vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
					mesh_vertices_buffer_.bind_base(mesh_vertices_binding);
					mesh_indices_buffer_.bind_base(mesh_indices_binding);
					boundary_edges_buffer_.bind_base(boundary_edges_binding);
					counters_buffer_.bind_base(counters_binding);
					shader.set_uniform("uIso", terrain_iso);
					shader.set_uniform("uChunkCoord", settings_.chunk_coord);
					shader.set_uniform("uChunkGridSize", settings_.chunk_grid_size);
					shader.set_uniform("uChunkSize", settings_.chunk_size);
					shader.set_uniform("uWorldCenter", settings_.world_center);
					shader.set_uniform("uCellSize", terrain_cell_size);
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT
			}
		};

		gfx::ComputeDispatcher::run(passes);
		pending_readback_ = true;
	}

	TerrainGenerator::RawPipelineResult TerrainGenerator::readback()
	{
		RawPipelineResult result{};
		if (!pending_readback_) return result;

		glMemoryBarrier(GL_ALL_BARRIER_BITS);


		const auto boundary_vertex_count = boundary_vertex_counter_buffer_.read_one<std::uint32_t>();
		const auto [
			vertex_count,
			index_count,
			edge_count
		] = counters_buffer_.read_one<GpuCounters>();
		pending_readback_ = false;

		if (vertex_count == 0 || index_count == 0)
			return result;

		const auto gpu_mesh_vertices = mesh_vertices_buffer_.read<vec2>(vertex_count);
		const auto mesh_indices = mesh_indices_buffer_.read<std::uint32_t>(index_count);

		if (boundary_vertex_count > 0)
		{
			const auto gpu_boundary_vertices = boundary_vertices_buffer_.read<vec2>(boundary_vertex_count);
			result.boundary_vertices.reserve(gpu_boundary_vertices.size());
			for (const auto& vertex : gpu_boundary_vertices)
			{
				result.boundary_vertices.push_back(vertex);
			}
		}

		result.mesh_vertices.reserve(gpu_mesh_vertices.size());
		for (const auto& vertex : gpu_mesh_vertices)
		{
			result.mesh_vertices.push_back(vertex);
		}

		result.mesh_indices = mesh_indices;

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

	TerrainGenerator::RawPipelineResult TerrainGenerator::run()
	{
		dispatch();
		return readback();
	}

	void TerrainGenerator::reset_buffers()
	{
		const std::vector horizontal_ids(settings_.field_size.y * (settings_.field_size.x - 1u), -1);
		const std::vector vertical_ids(settings_.field_size.y * settings_.field_size.x, -1);

		horizontal_edge_ids_buffer_.write(std::span{ horizontal_ids.data(), horizontal_ids.size() });
		vertical_edge_ids_buffer_.write(std::span{ vertical_ids.data(), vertical_ids.size() });

		static constexpr std::uint32_t zero_counter{ 0 };
		boundary_vertex_counter_buffer_.write(std::span{ &zero_counter, 1u });

		static constexpr GpuCounters zero_counters{};
		counters_buffer_.write(std::span{ &zero_counters, 1u });
	}
}
