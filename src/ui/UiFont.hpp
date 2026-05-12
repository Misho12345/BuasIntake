#pragma once

#include "pch.hpp"

namespace game::ui
{
    Result<void>     initialize_ui_font();
    void             destroy_ui_font();
    const sf::Font&  ui_font();
}
