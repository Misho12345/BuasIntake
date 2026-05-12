#pragma once

#include "pch.hpp"


#include "resources/ResourceNode.hpp"
#include "terrain/TerrainGenerator.hpp"
#include "terrain/TerrainSurfaceSampler.hpp"

namespace game::terrain
{
    struct TerrainGenerationFieldView final
    {
        std::span<TerrainGenerator::FieldSample> global_field{};
        uvec2                                    global_field_size{ 0u, 0u };
        std::uint32_t                            seed{ 0u };
    };

    struct TerrainGenerationCallbacks final
    {
        std::function<std::size_t(ivec2)>                             global_field_index{};
        std::function<int(ivec2)>                                     solid_neighbor_count{};
        std::function<vec2(ivec2)>                                    global_sample_world_position{};
        std::function<float(vec2)>                                    normalized_depth{};
        std::function<std::optional<TerrainSurfaceAttachment>(ivec2)> exposed_surface_attachment{};
        std::function<bool(ivec2)>                                    has_water_neighbor{};
        std::function<bool(ivec2)>                                    is_valid_global_sample{};
        std::function<float(const TerrainGenerator::FieldSample&)>    dry_water_density{};
        std::function<void(const resources::ResourceNode&)>           add_resource_node{};
    };

    namespace terrain_generation_finalizer
    {
        std::vector<ivec2> initialize_visual_channels_and_smooth_caves(
            TerrainGenerationFieldView        view,
            const TerrainGenerationCallbacks& callbacks);
        void generate_resource_nodes(
            const TerrainGenerationFieldView& view,
            const TerrainGenerationCallbacks& callbacks);
    }
}
