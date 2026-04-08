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
		struct FieldSample final
		{
			float terrain{};
			float water{};
		};

		static constexpr std::uint32_t terrain_channel_index = 0u;
		static constexpr std::uint32_t water_channel_index = 1u;

		struct TerrainEdit final
		{
			vec4 position_radius_strength{};
			vec4 shape{};

			[[nodiscard]]
			static TerrainEdit make(const vec2 world_position, const float radius, const float signed_strength, const float falloff_exponent = 1.8f)
			{
				return {
					.position_radius_strength = { world_position.x, world_position.y, radius, signed_strength },
					.shape = { falloff_exponent, 0.0f, 0.0f, 0.0f }
				};
			}

			[[nodiscard]]
			static TerrainEdit dig(const vec2 world_position, const float radius, const float strength, const float falloff_exponent = 1.8f)
			{
				return make(world_position, radius, -std::abs(strength), falloff_exponent);
			}

			[[nodiscard]]
			static TerrainEdit place(const vec2 world_position, const float radius, const float strength, const float falloff_exponent = 1.8f)
			{
				return make(world_position, radius, std::abs(strength), falloff_exponent);
			}
		};

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
		~TerrainGenerator();

		TerrainGenerator(const TerrainGenerator&) = delete;
		TerrainGenerator& operator=(const TerrainGenerator&) = delete;
		TerrainGenerator(TerrainGenerator&& other) noexcept;
		TerrainGenerator& operator=(TerrainGenerator&& other) noexcept;

		void dispatch();
		void dispatch_edits(std::span<const TerrainEdit> edits);
		void dispatch_surface_rebuild(std::uint32_t channel_index, float iso);
		void upload_field(std::span<const FieldSample> field_samples);
		[[nodiscard]] std::vector<FieldSample> read_field() const;
		[[nodiscard]] bool try_readback(RawPipelineResult& result);
		[[nodiscard]] RawPipelineResult readback();
		[[nodiscard]] RawPipelineResult run();

	private:
		void reset_surface_buffers();
		void replace_completion_fence();
		void clear_completion_fence();
		void wait_for_completion();
		[[nodiscard]] bool is_readback_ready() const;
		[[nodiscard]] RawPipelineResult consume_readback();

		ChunkSettings settings_{};

		gfx::Shader terrain_shader_{};
		gfx::Shader terrain_edit_shader_{};
		gfx::Shader edge_shader_{};
		gfx::Shader mesh_shader_{};
		gfx::Texture2D field_texture_{};
		gfx::SSBO terrain_edit_buffer_{};

		gfx::SSBO boundary_vertices_buffer_{};
		gfx::SSBO horizontal_edge_ids_buffer_{};
		gfx::SSBO vertical_edge_ids_buffer_{};
		gfx::SSBO boundary_vertex_counter_buffer_{};
		gfx::SSBO mesh_vertices_buffer_{};
		gfx::SSBO mesh_indices_buffer_{};
		gfx::SSBO boundary_edges_buffer_{};
		gfx::SSBO counters_buffer_{};

		GLsync completion_fence_{ nullptr };
		bool pending_readback_{ false };
	};
}
