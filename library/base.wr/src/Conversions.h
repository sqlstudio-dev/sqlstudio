/*
 * Copyright (c) 2008, 2018, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2026 dev4fun. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA 
 */

#pragma once

namespace MySQL {

public
  enum class ColorScheme {
    ColorSchemeStandard = base::ColorSchemeStandard,
    ColorSchemeStandardWin7 = base::ColorSchemeStandardWin7,
    ColorSchemeStandardWin8 = base::ColorSchemeStandardWin8,
    ColorSchemeStandardWin8Alternate = base::ColorSchemeStandardWin8Alternate,
    ColorSchemeHighContrast = base::ColorSchemeHighContrast,
    ColorSchemeDark = base::ColorSchemeDark,
    ColorSchemeCustom = base::ColorSchemeCustom,
  };

public
  enum class ApplicationColor {
    AppColorMainTab = base::AppColorMainTab,
    AppColorMainBackground = base::AppColorMainBackground,
    AppColorPanelHeader = base::AppColorPanelHeader,
    AppColorPanelHeaderFocused = base::AppColorPanelHeaderFocused,
    AppColorPanelToolbar = base::AppColorPanelToolbar,
    AppColorPanelContentArea = base::AppColorPanelContentArea,
    AppColorTabUnselected = base::AppColorTabUnselected,
    AppColorBottomTabSelected = base::AppColorBottomTabSelected,
    AppColorTopTabSelectedFocused = base::AppColorTopTabSelectedFocused,
    AppColorTopTabSelectedUnfocused = base::AppColorTopTabSelectedUnfocused,
    AppColorStatusbar = base::AppColorStatusbar,
    AppColorControl = base::AppColorControl,
    AppColorBorder = base::AppColorBorder,
    AppColorSelection = base::AppColorSelection,
    AppColorHover = base::AppColorHover,
    AppColorPressed = base::AppColorPressed,
    AppColorDisabled = base::AppColorDisabled,
    AppColorSplitter = base::AppColorSplitter,
    AppColorAccent = base::AppColorAccent,
  };

  // Conversions from native to managed types.
  // Use this class only with native types that don't use templates (because they cannot be made public).
  // Use ConvUtils.h instead.
public
  ref class Conversions {
  public:
    static base::Color NativeToColor(System::Drawing::Color color);
    static System::Drawing::Color ColorToNative(base::Color color);

    static System::Drawing::Color GetApplicationColor(ApplicationColor color, bool foreground);
    static bool UseWin8Drawing();
    static bool InHighContrastMode();
    static bool InDarkMode();
    static void RefreshSystemColorScheme();

    static void SetColorScheme(ColorScheme newScheme);
    static void SetUiZoom(float zoom);
  };

} // namespace MySQL
