#include "pch.hpp"

#include "Game.hpp"

int main()
{
    game::Game app{{
        .title = "BuasIntake",
        .win_size = { 800, 600 },
        .clear_color = 0x0E1621_rgb,
    }};

    app.run();
}
