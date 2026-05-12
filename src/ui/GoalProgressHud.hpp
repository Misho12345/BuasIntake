#pragma once

#include "pch.hpp"

#include "world/PlanetRestorationGoal.hpp"

namespace game::ui
{
    class GoalProgressHud final
    {
    public:
        Result<void> initialize_assets();
        void         destroy_graphics_resources();

        void draw(sf::RenderWindow& window, const world::PlanetRestorationGoal& goal) const;

    private:
        void draw_progress_bar(sf::RenderWindow& window, const world::PlanetRestorationGoal& goal) const;
        void draw_win_overlay(sf::RenderWindow& window) const;

        sf::Font win_font_{};
    };
}
