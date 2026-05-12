#include "pch.hpp"

#include "ui/UiFont.hpp"

namespace game::ui
{
    namespace
    {
        sf::Font shared_ui_font{};
        bool     shared_ui_font_ready{ false };
    }

    Result<void> initialize_ui_font()
    {
        if (shared_ui_font_ready) return {};

        if (!shared_ui_font.openFromFile("assets/fonts/Cinzel-SemiBold.ttf"))
        {
            return fail("Failed to load font 'assets/fonts/Cinzel-SemiBold.ttf'");
        }

        shared_ui_font_ready = true;
        return {};
    }

    void destroy_ui_font()
    {
        shared_ui_font       = sf::Font{};
        shared_ui_font_ready = false;
    }

    const sf::Font& ui_font()
    {
        return shared_ui_font;
    }
}
