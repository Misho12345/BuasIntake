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

		explicit TerrainChunk(b2WorldId world_id, const ChunkSettings& settings = {});
		~TerrainChunk() = default;

		TerrainChunk(const TerrainChunk&) = delete;
		TerrainChunk& operator=(const TerrainChunk&) = delete;
		TerrainChunk(TerrainChunk&&) noexcept = default;
		TerrainChunk& operator=(TerrainChunk&&) noexcept = default;

		void draw_gl(const sf::View& view) const;
		void draw_water_gl(const sf::View& view) const;
		void dispatch_generation();
		void finalize_generation();
		void rebuild_from_field(std::span<const FieldSample> field_samples);
		[[nodiscard]] std::vector<FieldSample> readback_field() const;

		[[nodiscard]] ivec2 chunk_coord() const;
		[[nodiscard]] vec2 display_min() const;
		[[nodiscard]] vec2 display_max() const;
		void set_collision_enabled(bool enabled);

	private:
		[[nodiscard]] TerrainContour::ScoredResult generate_chunk();

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
		bool collision_enabled_{ true };
		bool generation_dispatched_{ false };
		bool generation_finalized_{ false };
	};
}
