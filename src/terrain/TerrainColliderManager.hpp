#pragma once

#include "pch.hpp"


#include "terrain/TerrainCollider.hpp"

namespace game::terrain
{
    class TerrainColliderManager final
    {
    public:
        struct WaterBlobCollider final
        {
            explicit WaterBlobCollider(const b2WorldId world_id) : collider{ world_id } {}

            TerrainCollider collider;

            vec2 bounds_min{ 0.0f, 0.0f };
            vec2 bounds_max{ 0.0f, 0.0f };
        };

        explicit TerrainColliderManager(const b2WorldId world_id = b2_nullWorldId) : world_id_{ world_id } {}

        void reset_world(const b2WorldId world_id)
        {
            world_id_ = world_id;
            clear_water_colliders();
        }

        void clear_water_colliders() { water_blob_colliders_.clear(); }

        WaterBlobCollider& emplace_water_blob() { return water_blob_colliders_.emplace_back(world_id_); }

        void update_active_water_colliders(const vec2 player_position, const float activation_padding)
        {
            for (auto& blob : water_blob_colliders_)
            {
                const bool active = player_position.x >= blob.bounds_min.x - activation_padding &&
                        player_position.x <= blob.bounds_max.x + activation_padding &&
                        player_position.y >= blob.bounds_min.y - activation_padding &&
                        player_position.y <= blob.bounds_max.y + activation_padding;

                blob.collider.set_water_enabled(active);
            }
        }

        bool empty() const { return water_blob_colliders_.empty(); }

    private:
        b2WorldId                      world_id_{ b2_nullWorldId };
        std::vector<WaterBlobCollider> water_blob_colliders_{};
    };
}
