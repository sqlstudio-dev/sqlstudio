// Copyright (c) 2026 dev4fun. All rights reserved.
// Licensed under the GNU General Public License, version 2.0.

#include "base/drawing.h"
#include "gtest/gtest.h"
#include <cmath>

#ifdef _WIN32
namespace {

class WindowsTheme : public testing::Test {
protected:
  void SetUp() override {
    HIGHCONTRASTW contrast = {};
    contrast.cbSize = sizeof(contrast);
    ASSERT_TRUE(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0));
    if (contrast.dwFlags & HCF_HIGHCONTRASTON)
      GTEST_SKIP() << "Palette checks require Windows High Contrast to be disabled.";
  }

  void TearDown() override {
    base::Color::set_active_scheme(base::ColorSchemeStandard);
  }

  static double luminance(const base::Color &color) {
    auto linear = [](double component) {
      return component <= 0.04045 ? component / 12.92 : std::pow((component + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.red) + 0.7152 * linear(color.green) + 0.0722 * linear(color.blue);
  }
};

TEST_F(WindowsTheme, DarkApplicationTextHasReadableContrast) {
  base::Color::set_active_scheme(base::ColorSchemeDark);
  for (int role = base::AppColorMainTab; role <= base::AppColorStatusbar; ++role) {
    auto foreground = base::Color::getApplicationColor(static_cast<base::ApplicationColor>(role), true);
    auto background = base::Color::getApplicationColor(static_cast<base::ApplicationColor>(role), false);
    ASSERT_TRUE(foreground.is_valid()) << role;
    ASSERT_TRUE(background.is_valid()) << role;
    EXPECT_GE((luminance(foreground) + 0.05) / (luminance(background) + 0.05), 4.5) << role;
  }
}

TEST_F(WindowsTheme, ControlStatesHaveReadableContrast) {
  base::Color::set_active_scheme(base::ColorSchemeDark);
  for (auto role : {base::AppColorControl, base::AppColorSelection, base::AppColorHover, base::AppColorPressed}) {
    auto foreground = base::Color::getApplicationColor(role, true);
    auto background = base::Color::getApplicationColor(role, false);
    EXPECT_GE((luminance(foreground) + 0.05) / (luminance(background) + 0.05), 4.5);
  }
}

TEST_F(WindowsTheme, CustomControlColorsRoundTrip) {
  const std::map<std::string, std::string> custom = {
    {"CustomColor:control:back", "#303030"}, {"CustomColor:control:fore", "#eeeeee"}
  };
  base::Color::load_custom_colors(custom);
  base::Color::set_active_scheme(base::ColorSchemeCustom);
  EXPECT_EQ(base::Color::getApplicationColorAsString(base::AppColorControl, false), "#303030");
  EXPECT_EQ(base::Color::getApplicationColorAsString(base::AppColorControl, true), "#eeeeee");
  EXPECT_TRUE(base::Color::getApplicationColor(base::AppColorBorder, false).is_valid());
  std::map<std::string, std::string> saved;
  base::Color::save_custom_colors(saved);
  EXPECT_EQ(saved["CustomColor:control:back"], "#303030");
  EXPECT_EQ(saved["CustomColor:control:fore"], "#eeeeee");
}

TEST_F(WindowsTheme, EditorBackgroundAndTextFollowDarkPalette) {
  base::Color::set_active_scheme(base::ColorSchemeDark);
  EXPECT_LT(luminance(base::Color::getSystemColor(base::TextBackgroundColor)), 0.05);
  EXPECT_GT(luminance(base::Color::getSystemColor(base::TextColor)), 0.5);
  EXPECT_EQ(base::Color::getSystemColor(base::TextBackgroundColor).to_html(),
            base::Color::getApplicationColor(base::AppColorPanelContentArea, false).to_html());
}

TEST_F(WindowsTheme, RefreshPreservesExplicitSelections) {
  for (auto scheme : {base::ColorSchemeDark, base::ColorSchemeStandardWin8, base::ColorSchemeStandardWin7,
                      base::ColorSchemeStandardWin8Alternate, base::ColorSchemeHighContrast}) {
    base::Color::set_active_scheme(scheme);
    EXPECT_FALSE(base::Color::refresh_system_scheme());
    EXPECT_EQ(base::Color::get_active_scheme(), scheme);
    EXPECT_EQ(base::Color::is_high_contrast_scheme(), scheme == base::ColorSchemeHighContrast);
  }
}

TEST_F(WindowsTheme, SystemSelectionResolvesWindowsAppPreference) {
  DWORD light = 1;
  DWORD size = sizeof(light);
  const auto status = RegGetValueW(HKEY_CURRENT_USER,
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
    RRF_RT_REG_DWORD, nullptr, &light, &size);
  base::Color::set_active_scheme(base::ColorSchemeStandard);
  EXPECT_EQ(base::Color::get_active_scheme() == base::ColorSchemeDark, status == ERROR_SUCCESS && light == 0);
  EXPECT_FALSE(base::Color::refresh_system_scheme());
}

// These values are persisted in user settings. Never renumber legacy schemes.
static_assert(base::ColorSchemeStandard == 0 && base::ColorSchemeStandardWin7 == 1 &&
              base::ColorSchemeStandardWin8 == 2 && base::ColorSchemeStandardWin8Alternate == 3 &&
              base::ColorSchemeHighContrast == 4 && base::ColorSchemeDark == 5 && base::ColorSchemeCustom == 128,
              "Color scheme persistence changed");
} // namespace
#endif
