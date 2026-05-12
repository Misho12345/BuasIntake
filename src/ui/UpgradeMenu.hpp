#pragma once

#include "pch.hpp"

namespace game::ui
{
    struct UpgradeStat final
    {
        std::string label{};
        std::string current{};
        std::string next{};
    };

    struct UpgradeCard final
    {
        std::string title{};

        sf::IntRect current_icon{};
        std::size_t current_level{ 0u };
        sf::IntRect next_icon{};
        std::size_t next_level{ 0u };

        std::vector<UpgradeStat> stats{};
        std::string              cost_text{};

        bool maxed{ false };
    };

    class UpgradeMenu final
    {
    public:
        // ai help was used for this simple hud/menu layout work
        void draw(
            sf::RenderTarget&            target,
            const sf::Font&              font,
            const sf::Texture&           tools_texture,
            std::span<const UpgradeCard> cards) const;

        static sf::FloatRect button_rect(uvec2 target_size, std::size_t action_index);
    };
}
