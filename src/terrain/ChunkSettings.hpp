#pragma once

#include "pch.hpp"


namespace game::terrain
{
    struct ChunkSettings final
    {
        uvec2 field_size{64, 64};
        uvec2 field_padding{1, 1};
        ivec2 chunk_coord{0, 0};
        ivec2 chunk_grid_size{10, 10};
        vec2 chunk_size{25.0f, 25.0f};
        vec2 world_center{0.0f, 0.0f};
        std::uint32_t seed{1337u};
        float planet_radius{12.0f};
    };
}
