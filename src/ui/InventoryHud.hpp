#pragma once

#include "pch.hpp"

namespace game::resources
{
    struct HudState;
}

namespace game::ui
{
    class InventoryHud final
    {
    public:
        Result<void> initialize_assets();

        void destroy_graphics_resources();
        void draw(sf::RenderTarget& target, const resources::HudState& hud_state) const;

    private:
        sf::Texture processed_resource_texture_{};
        sf::Texture seed_icon_texture_{};
        sf::Font    font_{};
        bool        assets_ready_{ false };
    };
}
