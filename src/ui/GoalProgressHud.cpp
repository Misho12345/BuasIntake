#include "pch.hpp"

#include "ui/GoalProgressHud.hpp"

namespace game::ui
{
    Result<void> GoalProgressHud::initialize_assets()
    {
        if (assets_ready_) return {};

        if (!win_font_.openFromFile("assets/fonts/Cinzel-SemiBold.ttf"))
        {
            return fail("Failed to load font 'assets/fonts/Cinzel-SemiBold.ttf'");
        }

        assets_ready_ = true;
        return {};
    }

    void GoalProgressHud::destroy_graphics_resources()
    {
        win_font_     = sf::Font{};
        assets_ready_ = false;
    }

    void GoalProgressHud::draw(sf::RenderWindow& window, const world::PlanetRestorationGoal& goal) const
    {
        draw_progress_bar(window, goal);
        if (assets_ready_ && goal.completed()) draw_win_overlay(window);
    }

    void GoalProgressHud::draw_progress_bar(sf::RenderWindow& window, const world::PlanetRestorationGoal& goal) const
    {
        const auto  target_size = window.getSize();
        const float ui_scale    = std::clamp(static_cast<float>(target_size.x) / 800.0f, 0.72f, 1.18f);

        const vec2 bar_size{
            std::max(80.0f, std::min(static_cast<float>(target_size.x) - 48.0f, 420.0f * ui_scale)),
            18.0f * ui_scale
        };

        const vec2 bar_position{
            (static_cast<float>(target_size.x) - bar_size.x) * 0.5f,
            18.0f * ui_scale
        };

        const float progress = goal.progress();

        sf::RectangleShape shadow{ { bar_size.x + 8.0f * ui_scale, bar_size.y + 8.0f * ui_scale } };
        shadow.setPosition({ bar_position.x - 4.0f * ui_scale, bar_position.y - 4.0f * ui_scale });
        shadow.setFillColor(0x030604A8_rgba);
        window.draw(shadow);

        sf::RectangleShape background{ bar_size };
        background.setPosition(bar_position);
        background.setFillColor(0x172014E6_rgba);
        background.setOutlineColor(0xD7F2C9D8_rgba);
        background.setOutlineThickness(std::max(1.0f, 1.5f * ui_scale));
        window.draw(background);

        const float inset = std::max(2.0f, 3.0f * ui_scale);

        const vec2 fill_size{
            std::max(0.0f, (bar_size.x - inset * 2.0f) * progress),
            std::max(1.0f, bar_size.y - inset * 2.0f)
        };

        sf::RectangleShape fill{ fill_size };
        fill.setPosition({ bar_position.x + inset, bar_position.y + inset });
        fill.setFillColor(0x68E85FFF_rgba);
        window.draw(fill);
    }

    void GoalProgressHud::draw_win_overlay(sf::RenderWindow& window) const
    {
        const auto target_size = window.getSize();
        const vec2 center{ static_cast<float>(target_size.x) * 0.5f, static_cast<float>(target_size.y) * 0.5f };

        sf::RectangleShape dim{ { static_cast<float>(target_size.x), static_cast<float>(target_size.y) } };
        dim.setFillColor(0x07120CBC_rgba);
        window.draw(dim);

        sf::Text title{ win_font_, "PLANET RESTORED", 54u };
        title.setFillColor(0x9CFF7CFF_rgba);
        title.setOutlineColor(0x061006E6_rgba);
        title.setOutlineThickness(2.4f);

        const auto title_bounds = title.getLocalBounds();
        title.setOrigin({
            title_bounds.position.x + title_bounds.size.x * 0.5f, title_bounds.position.y + title_bounds.size.y * 0.5f
        });

        title.setPosition({ center.x, center.y - 24.0f });
        window.draw(title);

        sf::Text subtitle{ win_font_, "The planet is green again", 22u };
        subtitle.setFillColor(0xECFFE7FF_rgba);
        subtitle.setOutlineColor(0x061006D0_rgba);
        subtitle.setOutlineThickness(1.4f);

        const auto subtitle_bounds = subtitle.getLocalBounds();

        subtitle.setOrigin({
            subtitle_bounds.position.x + subtitle_bounds.size.x * 0.5f,
            subtitle_bounds.position.y + subtitle_bounds.size.y * 0.5f
        });

        subtitle.setPosition({ center.x, center.y + 38.0f });

        window.draw(subtitle);
    }
}
