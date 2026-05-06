#pragma once

#include "pch.hpp"


#include "gfx/Mesh.hpp"
#include "gfx/Shader.hpp"

namespace game::terrain
{
    struct TerrainRenderable
    {
        TerrainRenderable() = default;
        ~TerrainRenderable() = default;

        TerrainRenderable(const TerrainRenderable&) = delete;
        TerrainRenderable& operator=(const TerrainRenderable&) = delete;
        TerrainRenderable(TerrainRenderable&&) noexcept = default;
        TerrainRenderable& operator=(TerrainRenderable&&) noexcept = default;

        Result<void> initialize();
        void draw(const gfx::Mesh& mesh, const sf::View& view) const;

        struct SharedAssets;

      private:
        std::shared_ptr<SharedAssets> assets_{};
    };
}
