#include "pch.hpp"

#include "ui/UpgradeMenu.hpp"

namespace game::ui
{
    namespace
    {
        constexpr sf::Vector2f preferred_upgrade_panel_size{1160.0f, 610.0f};
        constexpr float upgrade_menu_margin = 16.0f;

        struct UpgradeMenuLayout final
        {
            sf::Vector2f panel_position{};
            sf::Vector2f panel_size{};
            float scale{1.0f};
        };

        UpgradeMenuLayout make_upgrade_menu_layout(const sf::Vector2u target_size)
        {
            const sf::Vector2f available{std::max(static_cast<float>(target_size.x) - upgrade_menu_margin * 2.0f, 1.0f),
                                         std::max(static_cast<float>(target_size.y) - upgrade_menu_margin * 2.0f, 1.0f)};

            const float scale =
                std::min({available.x / preferred_upgrade_panel_size.x, available.y / preferred_upgrade_panel_size.y, 1.0f});

            const sf::Vector2f panel_size{preferred_upgrade_panel_size.x * scale, preferred_upgrade_panel_size.y * scale};

            return {.panel_position = {(static_cast<float>(target_size.x) - panel_size.x) * 0.5f,
                                       (static_cast<float>(target_size.y) - panel_size.y) * 0.5f},
                    .panel_size = panel_size,
                    .scale = scale};
        }

        std::string roman_tier(const std::size_t level_index)
        {
            static constexpr std::array tiers{ "I", "II", "III" };
            return tiers[std::min<std::size_t>(level_index, tiers.size() - 1u)];
        }
    }

    void UpgradeMenu::draw(sf::RenderTarget& target,
                           const sf::Font& font,
                           const sf::Texture& tools_texture,
                           const std::span<const UpgradeCard> cards) const
    {
        const auto target_size = target.getSize();
        const auto layout = make_upgrade_menu_layout(target_size);
        const auto scaled = [&](const float value) { return value * layout.scale; };
        const auto panel_point = [&](const float x, const float y)
        { return sf::Vector2f{layout.panel_position.x + scaled(x), layout.panel_position.y + scaled(y)}; };
        const auto scaled_size = [&](const float x, const float y) { return sf::Vector2f{scaled(x), scaled(y)}; };
        const auto scaled_thickness = [&](const float value) { return std::max(1.0f, scaled(value)); };

        sf::RectangleShape dim{{static_cast<float>(target_size.x), static_cast<float>(target_size.y)}};
        dim.setFillColor(0x020508B0_rgba);
        target.draw(dim);

        sf::RectangleShape panel{layout.panel_size};
        panel.setPosition(layout.panel_position);
        panel.setFillColor(0x0F1215FA_rgba);
        panel.setOutlineColor(0xD2A443FF_rgba);
        panel.setOutlineThickness(scaled_thickness(3.0f));
        target.draw(panel);

        sf::RectangleShape header_band{{layout.panel_size.x, scaled(82.0f)}};
        header_band.setPosition(layout.panel_position);
        header_band.setFillColor(0x231D14BE_rgba);
        target.draw(header_band);

        auto draw_text = [&](const std::string& value,
                             const sf::Vector2f position,
                             const unsigned int size,
                             const sf::Color color,
                             const bool centered = false,
                             const bool bold = false)
        {
            const auto scaled_character_size =
                static_cast<unsigned int>(std::max(12.0f, std::round(static_cast<float>(size) * layout.scale)));
            sf::Text text{font, value, scaled_character_size};
            if (bold) text.setStyle(sf::Text::Bold);
            text.setFillColor(color);
            text.setOutlineColor(0x03080CD2_rgba);
            text.setOutlineThickness(size >= 20u ? std::max(1.0f, scaled(1.7f)) : std::max(0.75f, scaled(1.0f)));
            if (centered)
            {
                const auto bounds = text.getLocalBounds();
                text.setOrigin({bounds.position.x + bounds.size.x * 0.5f, bounds.position.y + bounds.size.y * 0.5f});
            }
            text.setPosition(position);
            target.draw(text);
        };

        auto draw_roman_badge = [&](const sf::Vector2f center, const std::size_t level_index)
        {
            draw_text(roman_tier(level_index), {center.x + 2.0f, center.y + 2.0f}, 28u, 0x000000BE_rgba, true, true);
            draw_text(roman_tier(level_index), center, 28u, 0xFFDF5BFF_rgba, true, true);
        };

        auto draw_arrow = [&](const sf::Vector2f center,
                              const float shaft_length,
                              const float shaft_height,
                              const float head_length,
                              const float head_half_height)
        {
            sf::RectangleShape shaft{{shaft_length, shaft_height}};
            shaft.setOrigin({shaft_length * 0.5f, shaft_height * 0.5f});
            shaft.setPosition({center.x - head_length * 0.38f, center.y});
            shaft.setFillColor(0xEBBE4AFF_rgba);
            target.draw(shaft);

            sf::ConvexShape head{3u};
            head.setPoint(0u, {center.x + shaft_length * 0.5f, center.y});
            head.setPoint(1u, {center.x + shaft_length * 0.5f - head_length, center.y - head_half_height});
            head.setPoint(2u, {center.x + shaft_length * 0.5f - head_length, center.y + head_half_height});
            head.setFillColor(0xFFD352FF_rgba);
            head.setOutlineColor(0x52370CDC_rgba);
            head.setOutlineThickness(1.5f);
            target.draw(head);
        };

        auto draw_icon_box = [&](const sf::IntRect icon_rect, const sf::Vector2f center, const std::size_t level_index)
        {
            sf::RectangleShape shadow{scaled_size(162.0f, 162.0f)};
            shadow.setOrigin({scaled(81.0f), scaled(81.0f)});
            shadow.setPosition({center.x + scaled(4.0f), center.y + scaled(5.0f)});
            shadow.setFillColor(0x00000058_rgba);
            target.draw(shadow);

            sf::RectangleShape box{scaled_size(162.0f, 162.0f)};
            box.setOrigin({scaled(81.0f), scaled(81.0f)});
            box.setPosition(center);
            box.setFillColor(0x192126FC_rgba);
            box.setOutlineColor(0x657F87F5_rgba);
            box.setOutlineThickness(scaled_thickness(2.5f));
            target.draw(box);

            sf::Sprite icon{tools_texture, icon_rect};
            const auto bounds = icon.getLocalBounds();
            icon.setOrigin(bounds.getCenter());
            const float icon_scale = std::min(scaled(108.0f) / bounds.size.x, scaled(108.0f) / bounds.size.y);
            icon.setScale({icon_scale, icon_scale});
            icon.setPosition({center.x, center.y - scaled(9.0f)});
            target.draw(icon);
            draw_roman_badge({center.x, center.y + scaled(63.0f)}, level_index);
        };

        draw_text("Upgrade Bench", panel_point(preferred_upgrade_panel_size.x * 0.5f, 43.0f), 46u, 0xFFE797FF_rgba, true, true);

        for (std::size_t index = 0; index < cards.size(); ++index)
        {
            const auto& card_data = cards[index];
            const sf::Vector2f card_position = panel_point(34.0f + static_cast<float>(index) * 550.0f, 108.0f);
            sf::RectangleShape card{scaled_size(525.0f, 458.0f)};
            card.setPosition(card_position);
            card.setFillColor(0x171D22F6_rgba);
            card.setOutlineColor(0x576869F5_rgba);
            card.setOutlineThickness(scaled_thickness(2.0f));
            target.draw(card);

            draw_text(
                card_data.title, {card_position.x + scaled(262.5f), card_position.y + scaled(33.0f)}, 34u, 0xF3F5E0FF_rgba, true, true);
            draw_icon_box(
                card_data.current_icon, {card_position.x + scaled(149.0f), card_position.y + scaled(139.0f)}, card_data.current_level);
            draw_arrow({card_position.x + scaled(262.5f), card_position.y + scaled(139.0f)},
                       scaled(56.0f),
                       scaled(7.0f),
                       scaled(22.0f),
                       scaled(12.0f));
            draw_icon_box(card_data.next_icon, {card_position.x + scaled(376.0f), card_position.y + scaled(139.0f)}, card_data.next_level);

            const sf::Vector2f table_position{card_position.x + scaled(40.0f), card_position.y + scaled(244.0f)};
            for (std::size_t row = 0u; row < card_data.stats.size(); ++row)
            {
                const float y = table_position.y + static_cast<float>(row) * scaled(48.0f);
                sf::RectangleShape row_background{scaled_size(445.0f, 40.0f)};
                row_background.setPosition({table_position.x, y - scaled(20.0f)});
                row_background.setFillColor(row % 2u == 0u ? 0x1F282DE8_rgba : 0x1B2227E8_rgba);
                target.draw(row_background);

                draw_text(card_data.stats[row].label, {table_position.x + scaled(70.0f), y}, 22u, 0xE0E9E2FF_rgba, true, true);
                draw_text(card_data.stats[row].current, {table_position.x + scaled(210.0f), y}, 23u, 0xCDDADBFF_rgba, true, true);
                draw_arrow({table_position.x + scaled(292.0f), y}, scaled(42.0f), scaled(5.0f), scaled(15.0f), scaled(8.0f));
                draw_text(card_data.stats[row].next, {table_position.x + scaled(386.0f), y}, 23u, 0xFFE270FF_rgba, true, true);
            }

            const auto action_button_rect = button_rect(target_size, index);
            sf::RectangleShape button{action_button_rect.size};
            button.setPosition(action_button_rect.position);
            button.setFillColor(card_data.maxed ? 0x313435F5_rgba : 0xAC741DFC_rgba);
            button.setOutlineColor(card_data.maxed ? 0x676C6CE6_rgba : 0xFFDB5CFF_rgba);
            button.setOutlineThickness(scaled_thickness(2.0f));
            target.draw(button);

            draw_text(card_data.maxed ? "MAXED" : "UPGRADE",
                      {action_button_rect.getCenter().x, action_button_rect.getCenter().y - scaled(7.0f)},
                      24u,
                      card_data.maxed ? 0xAEB5B5FF_rgba : 0xFFF2BEFF_rgba,
                      true,
                      true);
            draw_text(card_data.maxed ? "" : card_data.cost_text,
                      {action_button_rect.getCenter().x, action_button_rect.getCenter().y + scaled(16.0f)},
                      17u,
                      0xFFE28EFF_rgba,
                      true,
                      true);
        }
    }

    sf::FloatRect UpgradeMenu::button_rect(const sf::Vector2u target_size, const std::size_t action_index)
    {
        const auto layout = make_upgrade_menu_layout(target_size);
        const float scale = layout.scale;
        const sf::Vector2f card_position{layout.panel_position.x + (34.0f + static_cast<float>(action_index) * 550.0f) * scale,
                                         layout.panel_position.y + 108.0f * scale};
        return {
            { card_position.x + 125.0f * scale, card_position.y + 389.0f * scale },
            { 275.0f * scale, 58.0f * scale }
        };
    }
}
