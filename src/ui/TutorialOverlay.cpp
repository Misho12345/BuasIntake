#include "pch.hpp"

#include "ui/TutorialOverlay.hpp"

#include "ui/UiFont.hpp"

namespace game::ui
{
    namespace
    {
        constexpr vec2  preferred_tutorial_panel_size{ 1180.0f, 740.0f };
        constexpr float tutorial_margin = 16.0f;

        struct TutorialSlide final
        {
            std::string_view title{};
            std::string_view body{};
        };

        constexpr std::array<TutorialSlide, TutorialOverlay::slide_count> tutorial_slides{{
            {
                "Shape the planet",
                "Left Click to dig. Right Click to place stored ground."
            },
            {
                "Find seeds underground",
                "Find dead plants in the caves. Left Click them to collect seeds which you'll use later for planting."
            },
            {
                "Fill the bucket",
                "Select the bucket (scroll) and Left Click cave water to scoop it up."
            },
            {
                "Water the surface",
                "Carry water outside. Right Click to preview, then Right Click again to pour."
            },
            {
                "Plant on wet ground",
                "Select the seed tool and Left Click wet ground to plant."
            },
            {
                "Upgrade your tools",
                "Mine ores underground. Press E to upgrade digging speed, storage, and bucket capacity."
            }
        }};

        struct TutorialLayout final
        {
            vec2  panel_position{};
            vec2  panel_size{};
            float scale{ 1.0f };
        };

        TutorialLayout make_tutorial_layout(const uvec2 target_size)
        {
            const vec2 available{
                std::max(static_cast<float>(target_size.x) - tutorial_margin * 2.0f, 1.0f),
                std::max(static_cast<float>(target_size.y) - tutorial_margin * 2.0f, 1.0f)
            };

            const float scale = std::min({
                available.x / preferred_tutorial_panel_size.x,
                available.y / preferred_tutorial_panel_size.y,
                1.0f
            });

            const vec2 panel_size{
                preferred_tutorial_panel_size.x * scale,
                preferred_tutorial_panel_size.y * scale
            };

            return {
                .panel_position = {
                    (static_cast<float>(target_size.x) - panel_size.x) * 0.5f,
                    (static_cast<float>(target_size.y) - panel_size.y) * 0.5f
                },
                .panel_size = panel_size,
                .scale      = scale
            };
        }

        std::vector<std::string> wrap_text(
            const sf::Font&       font,
            const std::string_view value,
            const unsigned int    character_size,
            const float           max_width)
        {
            std::vector<std::string> lines;
            std::istringstream       words{ std::string{ value } };
            std::string              word;
            std::string              line;

            while (words >> word)
            {
                const std::string candidate = line.empty() ? word : line + ' ' + word;
                sf::Text          text{ font, candidate, character_size };
                if (!line.empty() && text.getLocalBounds().size.x > max_width)
                {
                    lines.push_back(line);
                    line = word;
                    continue;
                }

                line = candidate;
            }

            if (!line.empty()) lines.push_back(line);
            return lines;
        }
    }

    Result<void> TutorialOverlay::initialize_assets()
    {
        if (assets_ready_) return {};

        if (!tutorial_texture_.loadFromFile("assets/images/tutorial.png"))
        {
            return fail("Failed to load tutorial sprite sheet 'assets/images/tutorial.png'");
        }

        tutorial_texture_.setSmooth(false);

        assets_ready_ = true;
        active_       = true;
        current_slide_ = 0u;
        return {};
    }

    void TutorialOverlay::destroy_graphics_resources()
    {
        tutorial_texture_ = sf::Texture{};
        assets_ready_     = false;
    }

    void TutorialOverlay::draw(sf::RenderTarget& target) const
    {
        if (!active_ || !assets_ready_) return;

        const auto target_size = target.getSize();
        const auto layout      = make_tutorial_layout(target_size);
        const auto slide_index = std::min(current_slide_, slide_count - 1u);
        const auto& slide      = tutorial_slides[slide_index];

        const auto scaled = [&](const float value) { return value * layout.scale; };
        const auto panel_point = [&](const float x, const float y)
        {
            return vec2{
                layout.panel_position.x + scaled(x),
                layout.panel_position.y + scaled(y)
            };
        };

        const auto scaled_size      = [&](const float x, const float y) { return vec2{ scaled(x), scaled(y) }; };
        const auto scaled_thickness = [&](const float value) { return std::max(1.0f, scaled(value)); };

        sf::RectangleShape dim{
            {
                static_cast<float>(target_size.x),
                static_cast<float>(target_size.y)
            }
        };
        dim.setFillColor(0x020508C8_rgba);
        target.draw(dim);

        sf::RectangleShape panel{ layout.panel_size };
        panel.setPosition(layout.panel_position);
        panel.setFillColor(0x0F1215FA_rgba);
        panel.setOutlineColor(0xD2A443FF_rgba);
        panel.setOutlineThickness(scaled_thickness(3.0f));
        target.draw(panel);

        sf::RectangleShape header_band{ { layout.panel_size.x, scaled(82.0f) } };
        header_band.setPosition(layout.panel_position);
        header_band.setFillColor(0x231D14BE_rgba);
        target.draw(header_band);

        auto draw_text = [&](
            const std::string& value,
            const vec2         position,
            const unsigned int size,
            const sf::Color    color,
            const bool         centered = false,
            const bool         bold     = false,
            const bool         outlined = true)
        {
            const auto scaled_character_size = static_cast<std::uint32_t>(std::max(
                12.0f,
                std::round(static_cast<float>(size) * layout.scale)));

            sf::Text text{ ui_font(), value, scaled_character_size };
            if (bold) text.setStyle(sf::Text::Bold);
            text.setFillColor(color);
            if (outlined)
            {
                text.setOutlineColor(0x03080CD2_rgba);
                text.setOutlineThickness(size >= 28u ? std::max(1.0f, scaled(1.7f)) : std::max(0.75f, scaled(0.75f)));
            }

            if (centered)
            {
                const auto bounds = text.getLocalBounds();
                text.setOrigin({
                    std::round(bounds.position.x + bounds.size.x * 0.5f),
                    std::round(bounds.position.y + bounds.size.y * 0.5f)
                });
            }

            text.setPosition({ std::round(position.x), std::round(position.y) });
            target.draw(text);
        };

        draw_text("Tutorial", panel_point(preferred_tutorial_panel_size.x * 0.5f, 43.0f), 46u, 0xFFE797FF_rgba, true, true);

        sf::RectangleShape image_frame{ scaled_size(1060.0f, 440.0f) };
        image_frame.setPosition(panel_point(60.0f, 100.0f));
        image_frame.setFillColor(0x171D22F6_rgba);
        image_frame.setOutlineColor(0x576869F5_rgba);
        image_frame.setOutlineThickness(scaled_thickness(2.0f));
        target.draw(image_frame);

        const auto texture_size = tutorial_texture_.getSize();
        const int  slide_width  = static_cast<int>(texture_size.x / slide_count);
        const int  slide_height = static_cast<int>(texture_size.y);

        sf::Sprite image{ tutorial_texture_, sf::IntRect{
            { static_cast<int>(slide_index) * slide_width, 0 },
            { slide_width, slide_height }
        } };

        const auto image_bounds = image.getLocalBounds();
        image.setOrigin({ image_bounds.position.x, image_bounds.position.y });
        const vec2 image_max_size{ scaled(1030.0f), scaled(410.0f) };
        const float image_scale = std::min(image_max_size.x / image_bounds.size.x, image_max_size.y / image_bounds.size.y);
        image.setScale({ image_scale, image_scale });
        image.setPosition({
            layout.panel_position.x + scaled(60.0f) + (scaled(1060.0f) - image_bounds.size.x * image_scale) * 0.5f,
            layout.panel_position.y + scaled(100.0f) + (scaled(440.0f) - image_bounds.size.y * image_scale) * 0.5f
        });
        target.draw(image);

        draw_text(
            std::format("{}. {}", slide_index + 1u, slide.title),
            panel_point(590.0f, 566.0f), 35u, 0xF3F5E0FF_rgba,
            true, true);

        const auto body_character_size = static_cast<std::uint32_t>(std::max(12.0f, std::round(25.0f * layout.scale)));
        const auto body_lines = wrap_text(ui_font(), slide.body, body_character_size, scaled(1010.0f));
        for (std::size_t line_index = 0u; line_index < body_lines.size(); ++line_index)
        {
            draw_text(
                body_lines[line_index],
                panel_point(590.0f, 615.0f + static_cast<float>(line_index) * 32.0f),
                25u,
                0xDCE7E0FF_rgba,
                true,
                false,
                false);
        }

        const auto draw_button = [&](const sf::FloatRect rect, const std::string& label, const bool highlighted)
        {
            sf::RectangleShape button{ rect.size };
            button.setPosition(rect.position);
            button.setFillColor(highlighted ? 0xAC741DFC_rgba : 0x1B2227F5_rgba);
            button.setOutlineColor(highlighted ? 0xFFDB5CFF_rgba : 0x657F87F5_rgba);
            button.setOutlineThickness(scaled_thickness(2.0f));
            target.draw(button);

            draw_text(label, rect.getCenter(), 22u, highlighted ? 0xFFF2BEFF_rgba : 0xDCE7E0FF_rgba, true, true, false);
        };

        draw_button(next_button_rect(target_size), slide_index + 1u == slide_count ? "Start" : "Next", true);
        draw_button(close_button_rect(target_size), "Skip", false);

        draw_text(
            std::format("{}/{}", slide_index + 1u, slide_count),
            panel_point(590.0f, 704.0f),
            18u,
            0xDCE7E0FF_rgba,
            true,
            true,
            false);
    }

    void TutorialOverlay::next_slide()
    {
        if (!active_) return;
        if (current_slide_ + 1u >= slide_count)
        {
            close();
            return;
        }

        ++current_slide_;
    }

    void TutorialOverlay::previous_slide()
    {
        if (!active_ || current_slide_ == 0u) return;
        --current_slide_;
    }

    void TutorialOverlay::close()
    {
        active_ = false;
    }

    void TutorialOverlay::handle_click(const vec2 ui_position, const uvec2 target_size)
    {
        if (!active_) return;
        if (next_button_rect(target_size).contains(ui_position))
        {
            next_slide();
            return;
        }

        if (close_button_rect(target_size).contains(ui_position)) close();
    }

    sf::FloatRect TutorialOverlay::next_button_rect(const uvec2 target_size)
    {
        const auto layout = make_tutorial_layout(target_size);
        return {
            layout.panel_position + vec2{ 985.0f, 683.0f } * layout.scale,
            vec2{ 120.0f, 42.0f } * layout.scale
        };
    }

    sf::FloatRect TutorialOverlay::close_button_rect(const uvec2 target_size)
    {
        const auto layout = make_tutorial_layout(target_size);
        return {
            layout.panel_position + vec2{ 990.0f, 23.0f } * layout.scale,
            vec2{ 115.0f, 38.0f } * layout.scale
        };
    }
}
