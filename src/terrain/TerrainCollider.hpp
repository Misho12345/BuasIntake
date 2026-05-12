#pragma once

#include "pch.hpp"


namespace game::terrain
{
    // box2d collider owned by a terrain chunk
    class TerrainCollider final
    {
    public:
        explicit TerrainCollider(b2WorldId world_id);
        ~TerrainCollider();

        TerrainCollider(const TerrainCollider&)            = delete;
        TerrainCollider& operator=(const TerrainCollider&) = delete;
        TerrainCollider(TerrainCollider&& other) noexcept;
        TerrainCollider& operator=(TerrainCollider&& other) noexcept;

        void build(
            const std::vector<std::vector<vec2>>& loops,
            const std::vector<std::vector<vec2>>& paths);

    private:
        b2WorldId world_id_{ b2_nullWorldId };
        b2BodyId  terrain_body_{ b2_nullBodyId };
    };
}
