#pragma once

#include "pch.hpp"

#include "terrain/TerrainChunkGrid.hpp"
#include "terrain/TerrainField.hpp"
#include "terrain/TerrainGenerationFinalizer.hpp"

namespace game::resources
{
    class ResourceSystem;
}

namespace game::vegetation
{
    class VegetationSystem;
}

namespace game::terrain
{
    class TerrainGenerationCoordinator final
    {
    public:
        using RecomputeWetness = std::function<void(const std::vector<ivec2>&, std::vector<bool>&)>;
        using RebuildChunks    = std::function<Result<void>(const std::vector<bool>&)>;

        void finalize_generated_field(
            TerrainField&                       field,
            const TerrainChunkGrid&             chunk_grid,
            resources::ResourceSystem*          resources,
            vegetation::VegetationSystem*       vegetation,
            TerrainGenerationFieldView          view,
            const TerrainGenerationCallbacks&   callbacks,
            const RecomputeWetness&             recompute_wetness,
            const RebuildChunks&                rebuild_chunks) const;
    };
}
