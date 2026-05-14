#pragma once

#include "pch.hpp"

namespace game::ui
{
    class TutorialOverlay final
    {
    public:
        static constexpr std::size_t slide_count{ 8u };

        Result<void> initialize_assets();
        void         destroy_graphics_resources();

        void draw(sf::RenderTarget& target) const;

        bool active() const { return active_; }
        void next_slide();
        void previous_slide();
        void close();

        void handle_click(vec2 ui_position, uvec2 target_size);

    private:
        static sf::FloatRect next_button_rect(uvec2 target_size);
        static sf::FloatRect close_button_rect(uvec2 target_size);

        sf::Texture tutorial_texture_{};

        std::size_t current_slide_{ 0u };
        bool        active_{ true };
        bool        assets_ready_{ false };
    };
}
