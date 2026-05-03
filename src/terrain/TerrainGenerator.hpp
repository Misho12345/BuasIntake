#pragma once

#include "pch.hpp"

#include "ChunkSettings.hpp"
#include "gfx/SSBO.hpp"
#include "gfx/Shader.hpp"
#include "gfx/Texture2D.hpp"

namespace game::terrain
{
	struct GroundBrushBlocker final
	{
		vec2 center{ 0.0f, 0.0f };
		vec2 right{ 1.0f, 0.0f };
		vec2 up{ 0.0f, 1.0f };
		vec2 half_extents{ 0.0f, 0.0f };
	};

	class TerrainGenerator final
	{
	public:
		struct FieldSample final
		{
			float terrain{};
			float water{};
			float wetness{};
			float padding{};
		};

		struct TerrainEditSummary final
		{
			std::uint32_t removed_units{ 0u };
			std::uint32_t placed_units{ 0u };
			std::uint32_t changed_any{ 0u };
			std::uint32_t touched_water_or_wet{ 0u };
			std::uint32_t applied_samples{ 0u };
			std::uint32_t padding0{ 0u };
			ivec2 changed_min{ 0, 0 };
			ivec2 changed_max{ -1, -1 };
		};

		struct TerrainEditCandidate final
		{
			ivec2 pixel{ 0, 0 };
			float distance_to_center{ 0.0f };
			float padding{ 0.0f };
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

		TerrainGenerator() = default;
		~TerrainGenerator();

		TerrainGenerator(const TerrainGenerator&) = delete;
		TerrainGenerator& operator=(const TerrainGenerator&) = delete;
		TerrainGenerator(TerrainGenerator&& other) noexcept;
		TerrainGenerator& operator=(TerrainGenerator&& other) noexcept;

		[[nodiscard]] Result<void> initialize(const ChunkSettings& settings);
		[[nodiscard]] Result<void> dispatch();
		[[nodiscard]] Result<void> apply_edits(std::span<const TerrainEdit> edits,
			std::uint32_t unit_budget = std::numeric_limits<std::uint32_t>::max(),
			const std::optional<GroundBrushBlocker>& blocker = std::nullopt);
		[[nodiscard]] Result<void> smooth_water_field(std::uint32_t iterations = 2u);
		[[nodiscard]] Result<void> dispatch_surface_rebuild(std::uint32_t channel_index, float iso);
		[[nodiscard]] Result<void> upload_field(std::span<const FieldSample> field_samples);
		[[nodiscard]] Result<std::vector<FieldSample>> read_field() const;
		[[nodiscard]] Result<TerrainEditSummary> read_edit_summary();
		[[nodiscard]] Result<std::vector<TerrainEditCandidate>> collect_edit_candidates(
			const TerrainEdit& edit,
			ivec2 dispatch_origin,
			ivec2 dispatch_size,
			const std::optional<GroundBrushBlocker>& blocker);
		[[nodiscard]] Result<void> apply_selected_edit_candidates(
			const TerrainEdit& edit,
			std::span<const TerrainEditCandidate> selected_candidates,
			const std::optional<GroundBrushBlocker>& blocker);
		[[nodiscard]] Result<RawPipelineResult> readback();

	private:
		struct FieldLayout final
		{
			uvec2 padded_size{ 0u, 0u };
			vec2 cell_size{ 0.0f, 0.0f };
			vec2 field_origin{ 0.0f, 0.0f };
			ivec2 field_padding{ 0, 0 };
		};

		void reset_surface_buffers();
		[[nodiscard]] Result<void> replace_completion_fence();
		void clear_completion_fence();
		[[nodiscard]] Result<void> wait_for_completion();
		[[nodiscard]] FieldLayout field_layout() const;
		void bind_chunk_uniforms(const gfx::Shader& shader, const FieldLayout& layout) const;
		void bind_generation_pass(const gfx::Shader& shader, const FieldLayout& layout) const;
		void bind_cave_pass(const gfx::Shader& shader, const FieldLayout& layout) const;
		void bind_pond_pass(const gfx::Shader& shader, const FieldLayout& layout) const;
		void bind_edit_pass(const gfx::Shader& shader, const FieldLayout& layout,
			std::span<const TerrainEdit> edits, std::uint32_t unit_budget, ivec2 dispatch_origin, ivec2 dispatch_size,
			const std::optional<GroundBrushBlocker>& blocker) const;
		void bind_water_smooth_pass(const gfx::Shader& shader, const gfx::Texture2D& input_texture,
			const gfx::Texture2D& output_texture) const;
		void bind_surface_edge_pass(const gfx::Shader& shader, const FieldLayout& layout,
			std::uint32_t channel_index, float iso) const;
		void bind_surface_mesh_pass(const gfx::Shader& shader, const FieldLayout& layout,
			std::uint32_t channel_index, float iso) const;
		[[nodiscard]] Result<RawPipelineResult> consume_readback();

		ChunkSettings settings_{};

		gfx::Shader terrain_shader_{};
		gfx::Shader cave_shader_{};
		gfx::Shader pond_shader_{};
		gfx::Shader terrain_edit_candidates_shader_{};
		gfx::Shader terrain_edit_shader_{};
		gfx::Shader water_smooth_shader_{};
		gfx::Shader edge_shader_{};
		gfx::Shader mesh_shader_{};
		gfx::Texture2D field_texture_{};
		gfx::Texture2D scratch_field_texture_{};

		gfx::SSBO terrain_edits_buffer_{};
		gfx::SSBO terrain_edit_summary_buffer_{};
		gfx::SSBO terrain_edit_candidates_buffer_{};
		gfx::SSBO terrain_edit_candidate_count_buffer_{};
		gfx::SSBO terrain_edit_selected_candidates_buffer_{};
		gfx::SSBO boundary_vertices_buffer_{};
		gfx::SSBO horizontal_edge_ids_buffer_{};
		gfx::SSBO vertical_edge_ids_buffer_{};
		gfx::SSBO boundary_vertex_counter_buffer_{};
		gfx::SSBO mesh_vertices_buffer_{};
		gfx::SSBO mesh_indices_buffer_{};
		gfx::SSBO boundary_edges_buffer_{};
		gfx::SSBO counters_buffer_{};

		GLsync completion_fence_{ nullptr };
		GLsync edit_summary_fence_{ nullptr };
		bool pending_edit_summary_{ false };
		bool pending_readback_{ false };
	};
}
