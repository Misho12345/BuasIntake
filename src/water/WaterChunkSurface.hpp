#pragma once

#include "pch.hpp"

#include "gfx/Mesh.hpp"
#include "terrain/TerrainContour.hpp"
#include "terrain/TerrainGenerator.hpp"
#include "water/WaterRenderable.hpp"

namespace game::water
{
    // per-chunk water render surface built from the terrain generator's water contour output
    // TerrainChunk owns this beside the solid terrain mesh so water can rebuild and draw independently
    class WaterChunkSurface final
    {
    public:
        Result<void> initialize();

        void         draw_gl(const sf::View& view) const;
        Result<void> dispatch_rebuild(terrain::TerrainGenerator& generator) const;

        Result<void> finalize_rebuild(
            terrain::TerrainGenerator&    generator,
            const terrain::ChunkSettings& settings);

        void         rebuild_mesh(const std::vector<vec2>& vertices, const std::vector<std::uint32_t>& indices);

    private:
        WaterRenderable renderable_{};
        gfx::Mesh       mesh_{};
    };
}
