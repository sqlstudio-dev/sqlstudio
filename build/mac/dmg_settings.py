# dmgbuild settings for the SqlStudio .dmg (used by make_dmg.sh).
#
# The window shows build/mac/background.png (540x405; background@2x.png is
# picked up for Retina screens) with the app and the Applications link placed
# on the two tiles the background draws.

import os.path

application = defines["app"]  # noqa: F821 (provided by dmgbuild)
app_name = os.path.basename(application)

format = "UDZO"
filesystem = "HFS+"

files = [application]
symlinks = {"Applications": "/Applications"}

background = defines["background"]  # noqa: F821
window_rect = ((200, 120), (540, 405))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False

icon_size = 112
text_size = 12
icon_locations = {
    app_name: (138, 200),
    "Applications": (390, 200),
}
