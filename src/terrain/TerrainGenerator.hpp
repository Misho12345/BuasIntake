#pragma once

#include "pch.hpp"

#include "ChunkSettings.hpp"
#include "gfx/SSBO.hpp"
#include "gfx/Shader.hpp"
#include "gfx/Texture2D.hpp"

namespace game::terrain
{
	class TerrainGenerator final
	{
	public:
		struct BoundaryEdge final
		{
			std::uint32_t a{};
			std::uint32_t b{};
		};

		struct RawPipelineResult final
		{
			std::vector<vec2> boundary_vertices{};
			std::vector<BoundaryEdge> boundary_edges{};
			std::vector<vec2> mesh_vertices{};
			std::vector<std::uint32_t> mesh_indices{};
		};

		explicit TerrainGenerator(const ChunkSettings& settings);
		~TerrainGenerator() = default;

		TerrainGenerator(const TerrainGenerator&) = delete;
		TerrainGenerator& operator=(const TerrainGenerator&) = delete;
		TerrainGenerator(TerrainGenerator&&) noexcept = default;
		TerrainGenerator& operator=(TerrainGenerator&&) noexcept = default;

		void dispatch();
		[[nodiscard]] RawPipelineResult readback();
		[[nodiscard]] RawPipelineResult run();

	private:
		void reset_buffers();

		ChunkSettings settings_{};

		gfx::Shader terrain_shader_{};
		gfx::Shader edge_shader_{};
		gfx::Shader mesh_shader_{};
		gfx::Texture2D field_texture_{};

		gfx::SSBO boundary_vertices_buffer_{};
		gfx::SSBO horizontal_edge_ids_buffer_{};
		gfx::SSBO vertical_edge_ids_buffer_{};
		gfx::SSBO boundary_vertex_counter_buffer_{};
		gfx::SSBO mesh_vertices_buffer_{};
		gfx::SSBO mesh_indices_buffer_{};
		gfx::SSBO boundary_edges_buffer_{};
		gfx::SSBO counters_buffer_{};

		bool pending_readback_{ false };
	};
}
