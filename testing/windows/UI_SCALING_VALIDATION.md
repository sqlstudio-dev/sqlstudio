# Windows UI scaling validation (2026-10-28)

## Architecture and configuration

The former MainForm scaler divided the application zoom by Windows DPI and recursively called Control.Scale on newly added controls. That could cancel Windows enlargement and scale controls again when docked or reparented. Many custom trees, tabs, and native Cairo surfaces retained fixed pixel metrics.

WBControls/UiScale.cs now owns application typography and geometry. At WindowsUIScale=100, menus use their original system point size and primary text uses the system menu font as a minimum. The earlier forced 150%/175% enlargement was removed after comparison with the user's VS Code reference. Semantic roles cover menus, general UI, trees, tabs, grids, inputs, secondary text, and headings. WindowsUIScale offers additional 100/125/150/175/200% zoom; preference changes apply after restart.

Fonts remain point-based. Custom pixel metrics combine application zoom and DeviceDpi; WinForms handles monitor scaling of ordinary geometry. Font-based container autoscaling is converted to DPI-based autoscaling before enlarged fonts are applied. Weak bindings retain original font baselines and the application factor already applied to each control. Newly added controls, reparenting, and handle recreation queue a shared pass rather than recursively multiplying a subtree.

Only recognized text controls receive managed fonts. Scintilla scales incoming logical font settings through the native bridge; Cairo sidebar drawing uses shared native UiMetrics. Dark/light notifications retain the earlier deferred HWND style updates that fixed the Apply/Revert Win32Exception.

Auto-sized table layouts use intrinsic text/input minima rather than stretched current widths. Dialogs grow to fit; ObjectEditorView provides scrollable document minima when docked into a narrower host. Checkbox layout reserves space for the DPI-aware dark-mode glyph. Tab icons and close/busy indicators use matching drawing and measurement sizes.

App.config enables PerMonitorV2, the executable embeds the Windows compatibility/common-controls manifest, and startup enables visual styles before constructing controls. This follows Microsoft's [WinForms .NET Framework high-DPI opt-in requirements](https://learn.microsoft.com/en-us/dotnet/desktop/winforms/high-dpi-support-in-windows-forms?view=netframeworkdesktop-4.8).

## Modified components

- frontend/windows/WBControls: new UiScale.cs, project inclusion, ControlTheme attachment/glyph painting, and FlatTabControl metrics/icons.
- frontend/windows/SqlStudio: MainForm.cs replaces the old scaler and stores physical window bounds; Program.cs, App.config, Properties/app.manifest, and SqlStudio.csproj enable framework DPI handling.
- frontend/windows/grt.db/ObjectEditorView.cs: scrollable minimum document dimensions.
- frontend/common/preferences_form.cpp and advanced_sidebar.cpp: zoom options and native sidebar text, spacing, and icon metrics.
- library/base/base/drawing.h, library/base/drawing.cpp, library/base.wr/src/Conversions.h and Conversions.cpp: shared native zoom value and managed bridge.
- library/forms/winforms/src/wf_app.cpp, wf_code_editor.cpp, wf_menu.cpp, wf_menubar.cpp, and wf_treeview.cpp: DPI reporting, Scintilla typography, menu attachment, and tree triangle metrics.
- ext/Aga.Controls/Tree/TreeViewAdv.Properties.cs and NodeControls/NodeIcon.cs, NodePlusMinus.cs, NodeCheckBox.cs: header/row support and scalable custom glyphs.
- testing/windows: new UiScaleSmoke.cs, extended ThemeControlsSmoke.cs and RunThemeControlsSmoke.ps1, and this report.

## Reproduction and results

Build the full SqlStudio.sln with Configuration=Debug, Platform=x64, and an absolute OutDir ending in a backslash. Validation used bin/x64/ScaleValidation, with runtime dependencies copied from the existing Debug build, to avoid replacing DLLs loaded by the running application.

Run from the repository root:

~~~powershell
./testing/windows/RunThemeControlsSmoke.ps1 -OutputDirectory ./bin/x64/ScaleValidation -TestName UiScaleSmoke
./testing/windows/RunThemeControlsSmoke.ps1 -OutputDirectory ./bin/x64/ScaleValidation
./bin/x64/ScaleValidation/studio.testing.exe --gtest_filter=WindowsTheme.*
python -m unittest discover -s testing/windows -p test_admin_theme.py
~~~

The original scaling validation build succeeded; see build/scale-build.log. The subsequent compact UI build and sidebar checks are recorded below.

- UiScaleSmoke: system-sized menu baseline, compact primary text proportions, dropdown inheritance, rows/tabs/glyphs, late controls, reparenting, repeated application, zoom round trip, and retained grid contents.
- DPI metric cases: 96, 120, 144, 168, and 192 DPI, then restoration of the original DPI. These simulate the framework's DeviceDpi updates; they do not change Windows settings.
- ThemeControlsSmoke: native System-style Button/CheckBox/RadioButton document opening; deferred HWND recreation during theme notifications; late controls; state preservation; rendered dark/light controls; actual Table Editor designer hierarchy; no table-name/schema overlap; room for checkbox text and glyphs; stable dialog sizing; moving real editor controls into a scrollable document and narrowing its host.
- All six native WindowsTheme tests pass; both Python administration theme tests pass.

Dark and light Table Editor screenshots were visually inspected at the host's 144 DPI. Generated files: bin/x64/ScaleValidation/theme-table-editor-dark.png and theme-table-editor-light.png. Test logs: build/scale-metrics-smoke.log, build/scale-theme-smoke.log, and build/scale-native-theme.log.

The designer fixture has no database backend: Load/Shown and the name field's backend Leave callback are detached only in the test. Production handlers remain intact.

## Limits and remaining manual coverage

Actual movement between monitors with different DPI, full application docking/tab reordering, every plugin dialog, long localized labels, and live database editing were not exercised end to end. The metric matrix is automated coverage, not a substitute for those hardware/application checks. Real editor reparenting, host resizing, and scroll minima are covered offline.

Native light-theme checkbox glyphs and OS scrollbars retain system rendering; their text and surrounding control dimensions are enlarged. Existing small raster assets are resampled and may be softer than vector assets. Legacy system-rendered scrollbars can remain light in dark mode. Cairo sidebar DPI currently follows the main application window.

The running application was not restarted. Use a rebuilt application after restart to see the changes.

## Startup access-violation regression

Reading WindowsUIScale in MainForm's constructor accessed WBContext options before Program.Main called wbContext.init. The native app_Application was still uninitialized; the resulting get_wb_options_value / ValueRef copy produced an access violation. Preference loading now runs at the start of MainForm.PostInit, which Program calls after backend initialization. The existing MainForm_Load pass applies the configured scale before normal use. No native access violation is caught or suppressed.

MainFormStartupSmoke creates the real native WbContext and managed MainForm in production order, without initializing application options. It reproduced the reported AccessViolationException with the prior SqlStudio.exe and passes after rebuilding the fix. Run it with RunThemeControlsSmoke.ps1 -TestName MainFormStartupSmoke; the runner supplies the Home icon and native bridge references. This is a focused constructor/lifetime regression test, not an end-to-end startup with plugins and a database.

The affected SqlStudio.csproj x64 Debug build passed with 0 errors and 0 warnings. Logs are build/scale-startup-before.log, build/scale-startup-build.log, and build/scale-startup-after.log. Validation uses bin/x64/ScaleValidation and does not replace the normal Debug executable.

## Compact sizing and home sidebar refinement

The user's side-by-side VS Code reference supersedes the earlier 150% menu / 175% control targets. Default application zoom is now neutral: menu text uses its original system size, primary controls use at least the system menu font size, and Windows DPI still applies once. Row heights, tab heights, toolbar icons, input padding, and native sidebar typography use compact baseline metrics. Explicit 125–200% application zoom remains available.

The Windows home sidebar uses a 48 DIP width, 28 DIP icon box, 8 DIP outer padding, and 40 DIP rows separated by 8 DIP gaps. Icons are centered and scaled from actual raster dimensions, avoiding a second enlargement of @2x assets. Drawing, hit testing, and accessibility bounds derive from the same actual sidebar width. Non-Windows sidebar sizing is retained. Changes are in library/forms/home_screen.cpp and mforms/home_screen.h.

CompactSidebar.IconsHavePaddingAndHitAreasFollowDpiAndZoom renders the actual native sidebar with real connection/model/migration icons. It checks raster padding/centering, row hit testing, and accessibility activation at 96/120/144/168/192 DPI with 100% and 150% zoom. Run studio.testing.exe --gtest_filter=CompactSidebar.* from the repository root so the test can find images/home assets; the test writes build/compact-sidebar-144dpi.png.

Managed compact metrics, theme/designer layout, and startup regression suites pass; logs are build/compact-ui-metrics.log, build/compact-ui-theme.log, and build/compact-ui-startup.log. Both compact Table Editor theme screenshots were visually inspected. Full build and native sidebar results are recorded in build/compact-ui-build.log and build/compact-ui-native.log.

Final compact UI verification: the complete x64 Debug solution build finished with 0 errors and 12 warnings (build/compact-ui-final-build.log). All seven native tests passed: six WindowsTheme cases and the CompactSidebar matrix. The sidebar raster at 144 DPI was visually inspected and shows centered, padded icons. UiScaleSmoke and MainFormStartupSmoke passed again against the final binaries. Actual monitor movement and a full live database session remain outside this validation.
