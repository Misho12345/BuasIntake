#pragma once

#include "pch.hpp"

namespace game::terrain
{
    class TerrainResourcePlacement final
    {
    public:
        template <typename IsValidSample>
        bool has_occupied_neighbor(const ivec2 coord, const int radius, IsValidSample&& is_valid_sample) const
        {
            for (int oy = -radius; oy <= radius; ++oy)
            {
                for (int ox = -radius; ox <= radius; ++ox)
                {
                    if (ox == 0 && oy == 0) continue;
                    const ivec2 neighbor{ coord.x + ox, coord.y + oy };
                    if (!is_valid_sample(neighbor)) continue;
                    if (is_occupied(neighbor)) return true;
                }
            }

            return false;
        }

        bool is_occupied(const ivec2 coord) const
        {
            return occupied_samples_.contains(game::sample_key(coord));
        }

        void occupy(const ivec2 coord)
        {
            occupied_samples_.insert(game::sample_key(coord));
        }

    private:
        std::unordered_set<std::uint64_t> occupied_samples_{};
    };
}
