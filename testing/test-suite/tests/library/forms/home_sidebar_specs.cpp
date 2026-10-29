// Copyright (c) 2026 dev4fun. Licensed under GPL-2.0.
#include "gtest/gtest.h"
#include "mforms/home_screen.h"
#include "mforms/app.h"
#include "mforms/utilities.h"
#include "stub/stub_mforms.h"
#include <cmath>

#ifdef _MSC_VER
namespace {
int sidebarWidth = 72;
int sidebarHeight = 400;
float screenScale = 1.5f;

TEST(CompactSidebar, IconsHavePaddingAndHitAreasFollowDpiAndZoom) {
  mforms::stub::init(nullptr);
  auto factory = mforms::ControlFactory::get_instance();
  auto originalView = factory->_view_impl;
  auto originalApp = factory->_app_impl;
  // Restore backend callbacks even if an assertion exits the test early.
  struct Restore {
    mforms::ControlFactory *factory;
    decltype(originalView) view;
    decltype(originalApp) app;
    ~Restore() { factory->_view_impl = view; factory->_app_impl = app; }
  } restore{factory, originalView, originalApp};
  factory->_view_impl.get_width = [](const mforms::View *) { return sidebarWidth; };
  factory->_view_impl.get_height = [](const mforms::View *) { return sidebarHeight; };
  factory->_app_impl.backing_scale_factor = [](mforms::App *) { return screenScale; };
  factory->_app_impl.get_resource_path = [](mforms::App *, const std::string &name) { return "images/home/" + name; };

  for (int dpi : {96, 120, 144, 168, 192}) for (double zoom : {1.0, 1.5}) {
    screenScale = dpi / 96.0f;
    double scale = screenScale * zoom;
    sidebarWidth = (int)std::round(48 * scale);
    sidebarHeight = (int)std::round(220 * scale);
    mforms::SidebarSection sidebar(nullptr);
    int clicks = 0;
    sidebar.addEntry("Connections", "sidebar_wb.png", nullptr, [&clicks]() { ++clicks; }, false);
    sidebar.addEntry("Models", "sidebar_modeling.png", nullptr, [&clicks]() { ++clicks; }, false);
    sidebar.addEntry("Migration", "sidebar_migration.png", nullptr, [&clicks]() { ++clicks; }, false);
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sidebarWidth, sidebarHeight);
    auto cr = cairo_create(surface);
    sidebar.repaint(cr, 0, 0, sidebarWidth, sidebarHeight);
    cairo_surface_flush(surface);
    auto data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    int inkLeft = sidebarWidth, inkRight = -1;
    for (int y = 0; y < sidebarHeight; ++y) for (int x = 0; x < sidebarWidth; ++x) {
      if (data[y * stride + x * 4 + 3] > 8) { inkLeft = std::min(inkLeft, x); inkRight = std::max(inkRight, x); }
    }
    EXPECT_GE(inkLeft, (int)std::floor(9 * scale)) << dpi << " DPI, zoom " << zoom;
    EXPECT_LE(inkRight, sidebarWidth - (int)std::floor(9 * scale));
    EXPECT_NEAR(inkLeft, sidebarWidth - 1 - inkRight, 2);
    EXPECT_EQ(-1, sidebar.shortcutFromPoint(0, (int)std::round(28 * scale)));
    EXPECT_EQ(-1, sidebar.shortcutFromPoint(sidebarWidth / 2, (int)std::round(52 * scale)));
    for (int i = 0; i < 3; ++i) {
      auto entry = sidebar.getAccessibilityChild(i);
      auto bounds = entry->getAccessibilityBounds();
      EXPECT_GT(bounds.size.width, 0);
      EXPECT_EQ(i, sidebar.shortcutFromPoint((int)bounds.center().x, (int)bounds.center().y));
      entry->accessibilityDoDefaultAction();
    }
    EXPECT_EQ(3, clicks);
    if (dpi == 144 && zoom == 1.0)
      EXPECT_EQ(CAIRO_STATUS_SUCCESS, cairo_surface_write_to_png(surface, "build/compact-sidebar-144dpi.png"));
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
  }
}
} // namespace
#endif
