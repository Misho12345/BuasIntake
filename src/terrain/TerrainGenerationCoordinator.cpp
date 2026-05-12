#include "pch.hpp"

#include "terrain/TerrainGenerationCoordinator.hpp"

#include "resources/ResourceSystem.hpp"
#include "vegetation/VegetationSystem.hpp"

namespace game::terrain
{
    void TerrainGenerationCoordinator::finalize_generated_field(
        TerrainField&                     field,
        const TerrainChunkGrid&           chunk_grid,
        resources::ResourceSystem*        resources,
        vegetation::VegetationSystem*     vegetation,
        TerrainGenerationFieldView        view,
        const TerrainGenerationCallbacks& callbacks,
        const RecomputeWetness&           recompute_wetness,
        const RebuildChunks&              rebuild_chunks) const
    {
        if (field.empty()) return;

        if (vegetation != nullptr) vegetation->initialize(field.sample_count());
        if (resources != nullptr) resources->clear_nodes();
        if (resources != nullptr) resources->reserve_nodes(16000u);

        auto dirty_chunks = chunk_grid.make_dirty_chunk_flags(true);
        auto changed_coords = terrain_generation_finalizer::initialize_visual_channels_and_smooth_caves(view, callbacks);

        if (recompute_wetness) recompute_wetness(changed_coords, dirty_chunks);
        terrain_generation_finalizer::generate_resource_nodes(view, callbacks);
        if (rebuild_chunks)
        {
            if (const auto finalize_result = rebuild_chunks(dirty_chunks);
                !finalize_result)
                Log::error(finalize_result.error());
        }
    }
}
