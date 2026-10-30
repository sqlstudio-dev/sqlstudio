// Copyright (c) 2026 dev4fun. Licensed under GPL-2.0.
#include "gtest/gtest.h"
#include "mforms/home_screen_connections.h"
#include "mforms/app.h"
#include "stub/stub_mforms.h"
#include <cmath>
#include <vector>
#include <filesystem>

#ifdef _MSC_VER
#include <cairo/cairo-win32.h>
#endif

namespace {
int welcomeWidth = 1600;
int welcomeHeight = 600;

TEST(HomeWelcomeLayout, FirstPaintFitsAllContentWithoutRelayout) {
  const auto imageDir = std::filesystem::absolute(std::filesystem::path("..") / ".." / "images" / "home").u8string();
  ASSERT_TRUE(std::filesystem::exists(std::filesystem::path(imageDir) / "home_screen_close_light.png"));
  mforms::stub::init(nullptr);
  auto factory = mforms::ControlFactory::get_instance();
  auto originalView = factory->_view_impl;
  auto originalApp = factory->_app_impl;
  struct Restore {
    mforms::ControlFactory *factory;
    decltype(originalView) view;
    decltype(originalApp) app;
    ~Restore() { factory->_view_impl = view; factory->_app_impl = app; }
  } restore{factory, originalView, originalApp};
  factory->_view_impl.get_width = [](const mforms::View *) { return welcomeWidth; };
  factory->_view_impl.get_height = [](const mforms::View *) { return welcomeHeight; };
  factory->_app_impl.backing_scale_factor = [](mforms::App *) { return 1.0f; };
  static std::string resourceDir;
  resourceDir = imageDir;
  factory->_app_impl.get_resource_path = [](mforms::App *, const std::string &name) {
    return (std::filesystem::path(resourceDir) / name).u8string();
  };

  const bool nativeSurfaces[] = {false,
#ifdef _MSC_VER
                                true
#endif
  };
  mforms::HomeScreen home;
  for (bool nativeSurface : nativeSurfaces) for (int width : {1024, 1600, 2560}) {
    SCOPED_TRACE(nativeSurface ? "Windows surface" : "Image surface");
    SCOPED_TRACE(width);
    welcomeWidth = width;
    mforms::ConnectionsWelcomeScreen welcome(&home);
    welcome.updateColors();
    welcome.updateIcons();

    const auto firstSize = welcome.getLayoutSize(base::Size(width, 0));
    EXPECT_EQ(width, firstSize.width);
    EXPECT_EQ(firstSize.height, welcome.getLayoutSize(base::Size(width, 600)).height);
    welcomeHeight = static_cast<int>(std::ceil(firstSize.height));

    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, 600);
#ifdef _MSC_VER
    if (nativeSurface) {
      cairo_surface_destroy(surface);
      surface = cairo_win32_surface_create_with_dib(CAIRO_FORMAT_ARGB32, width, 600);
    }
#endif
    auto cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, width, welcomeHeight);
    cairo_clip(cr);
    // Dispatch through DrawBox because the welcome screen's override is private.
    static_cast<mforms::DrawBox &>(welcome).repaint(cr, 0, 0, width, welcomeHeight);
    cairo_restore(cr);
    cairo_surface_flush(surface);
    auto image = surface;
#ifdef _MSC_VER
    if (nativeSurface)
      image = cairo_win32_surface_get_image(surface);
#endif
    auto data = cairo_image_surface_get_data(image);
    const int byteCount = cairo_image_surface_get_stride(image) * 600;
    const std::vector<unsigned char> firstPaint(data, data + byteCount);

    const auto settledSize = welcome.getLayoutSize(base::Size(width, 600));
    EXPECT_EQ(firstSize.height, settledSize.height) << "Painting must not change the required height.";
    for (size_t index = 1; index < welcome.getAccessibilityChildCount(); ++index) {
      auto link = welcome.getAccessibilityChild(index);
      auto bounds = link->getAccessibilityBounds();
      EXPECT_GT(bounds.size.height, 0) << link->getAccessibilityTitle();
      EXPECT_LE(bounds.bottom(), firstSize.height) << link->getAccessibilityTitle() << " is clipped on first paint.";
      EXPECT_EQ(link, welcome.accessibilityHitTest(
                        static_cast<ssize_t>(bounds.center().x), static_cast<ssize_t>(bounds.center().y)));
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    welcomeHeight = static_cast<int>(std::ceil(settledSize.height));
    static_cast<mforms::DrawBox &>(welcome).repaint(cr, 0, 0, width, welcomeHeight);
    cairo_surface_flush(surface);
    EXPECT_EQ(firstPaint, std::vector<unsigned char>(data, data + byteCount))
      << "The initial clipped viewport must render the same complete content as a later repaint.";
    EXPECT_EQ(CAIRO_STATUS_SUCCESS, cairo_status(cr));
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
  }
}
} // namespace
