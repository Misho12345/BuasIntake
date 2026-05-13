#include "pch.hpp"

#include "water/WaterChunkSurface.hpp"

#include "gfx/MeshBuilders.hpp"

namespace game::water
{
    Result<void> WaterChunkSurface::initialize() { return renderable_.initialize(); }

    void WaterChunkSurface::draw_gl(const sf::View& view) const { renderable_.draw(mesh_, view); }

    Result<void> WaterChunkSurface::dispatch_rebuild(terrain::TerrainGenerator& generator) const
    {
        return generator.dispatch_surface_rebuild(terrain::TerrainGenerator::water_channel_index, 0.0f);
    }

    Result<void> WaterChunkSurface::finalize_rebuild(
        terrain::TerrainGenerator&    generator,
        const terrain::ChunkSettings& settings)
    {
        auto readback_result = generator.readback();
        if (!readback_result) return fail(readback_result.error());

        auto water_result = terrain::TerrainContour::score_and_filter(std::move(*readback_result), settings);
        rebuild_mesh(water_result.mesh_vertices, water_result.mesh_indices);
        return {};
    }

    void WaterChunkSurface::rebuild_mesh(
        const std::vector<vec2>&          vertices,
        const std::vector<std::uint32_t>& indices)
    {
        if (vertices.empty() || indices.empty())
        {
            const std::vector<sf::Vertex>    empty_vertices;
            const std::vector<std::uint32_t> empty_indices;
            mesh_.set_data(empty_vertices, empty_indices);
            return;
        }

        const auto mesh_vertices = gfx::build_vertices(vertices, 0xE8F8FFC4_rgba);
        mesh_.set_data(mesh_vertices, indices);
    }
}
