#pragma once

#include "pch.hpp"

#include "ChunkSettings.hpp"
#include "TerrainCollider.hpp"
#include "TerrainContour.hpp"
#include "TerrainGenerator.hpp"
#include "TerrainRenderable.hpp"
#include "gfx/Mesh.hpp"
#include "water/WaterRenderable.hpp"

namespace game::terrain
{
	class TerrainChunk final
	{
	public:
		using TerrainEdit = TerrainGenerator::TerrainEdit;
		using FieldSample = TerrainGenerator::FieldSample;
		using TerrainEditSummary = TerrainGenerator::TerrainEditSummary;

		explicit TerrainChunk(b2WorldId world_id, const ChunkSettings& settings = {});
		~TerrainChunk() = default;

		TerrainChunk(const TerrainChunk&) = delete;
		TerrainChunk& operator=(const TerrainChunk&) = delete;
		TerrainChunk(TerrainChunk&&) noexcept = default;
		TerrainChunk& operator=(TerrainChunk&&) noexcept = default;

		[[nodiscard]] Result<void> initialize();
		void draw_gl(const sf::View& view) const;
		void draw_water_gl(const sf::View& view) const;
		[[nodiscard]] Result<void> dispatch_generation();
		[[nodiscard]] Result<void> finalize_generation();
		[[nodiscard]] Result<TerrainEditSummary> apply_ground_brush_gpu(const TerrainEdit& edit,
			std::uint32_t unit_budget = std::numeric_limits<std::uint32_t>::max(),
			const std::optional<GroundBrushBlocker>& blocker = std::nullopt);
		[[nodiscard]] Result<std::vector<FieldSample>> finalize_gpu_ground_brush();
		[[nodiscard]] Result<void> rebuild_from_field(std::span<const FieldSample> field_samples, bool smooth_water = false);
		[[nodiscard]] Result<std::vector<FieldSample>> readback_field() const;
		[[nodiscard]] bool has_pending_gpu_ground_brush() const;

		[[nodiscard]] ivec2 chunk_coord() const;
		[[nodiscard]] vec2 display_min() const;
		[[nodiscard]] vec2 display_max() const;

	private:
		[[nodiscard]] Result<TerrainContour::ScoredResult> read_scored_surface();
		[[nodiscard]] Result<TerrainContour::ScoredResult> rebuild_scored_surface(std::uint32_t channel_index, float iso);
		[[nodiscard]] Result<void> rebuild_chunk_meshes(std::span<const FieldSample> field_samples);

		void build_chunk(const TerrainContour::ScoredResult& terrain_result, const TerrainContour::ScoredResult& water_result,
			std::span<const FieldSample> field_samples);
		void build_terrain_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices,
			std::span<const FieldSample> field_samples);
		void build_water_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices);

		ChunkSettings settings_{};

		TerrainGenerator generator_;
		TerrainCollider collider_;
		TerrainRenderable renderable_{};
		water::WaterRenderable water_renderable_{};

		gfx::Mesh mesh_{};
		gfx::Mesh water_mesh_{};

		vec2 display_min_{};
		vec2 display_max_{};
		bool generation_dispatched_{ false };
		bool generation_finalized_{ false };
		bool pending_gpu_ground_brush_{ false };
	};
}
