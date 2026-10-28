# Windows theme validation

## Causes and shared fixes

The existing native palette already resolved Windows app appearance and explicit Light/Dark overrides. Managed controls nevertheless selected WinForms SystemColors.Window/Control in their dark branches; those values stay light in Windows dark mode. ControlTheme also omitted ordinary layout panels and buttons, and the mforms C++/CLI bridge had its attachment disabled. The Table Editor's designer white backgrounds therefore survived the existing theme notifications.

- library/base/drawing.cpp and drawing.h: extend ApplicationColor with control, border, selection, hover, pressed, disabled, splitter, and accent roles; expose them through base.wr/Conversions.h. Existing surface/header/toolbar roles remain in use. Preserve legacy enum values. Custom color loading and saving now round-trip actual values and new roles, with defaults for missing entries.
- WBControls/ControlTheme.cs: attach nested and dynamically inserted controls, restore light-mode styles, use the existing GNColorsChanged notification, and release observers and native combo hooks with HWNDs. Cover standard grids, toolbars, buttons, labels, checkboxes, radio buttons, splitters, panels, fields, and tabs. Keep native dropdown interaction and custom owner drawing; defer HWND-recreating properties until creation completes.
- FlatTabControl, Bevel, MySQLCsUtilities/GridView: theme scrollers, separators, grid lines, and selection. Aga.Controls: optional selected-row colors and readable selected text, leaving its native light/high-contrast path available.
- wf_view.cpp and wf_scrollpanel.cpp: restore shared attachment for native-backed mforms controls and scrollable layout surfaces. Custom drawing canvases retain their own painting.

## Reproduce

Build SqlStudio.sln with MSBuild, Configuration=Debug and Platform=x64. When the application is running, set OutDir to a separate absolute directory (with a trailing backslash) to avoid locked DLLs. Supply the application's runtime DLL dependencies in that directory.

Run testing/windows/RunThemeControlsSmoke.ps1, optionally with -OutputDirectory pointing to that build. The test checks repeated light/dark switching, System palette selection, nested layouts, late controls, handle recreation, retained content/selection/layout, disabled and pressed/hover states, and rendered tree selection. It instantiates the real Table Editor designer without a database backend and saves theme-table-editor-dark.png and theme-table-editor-light.png. Load/Shown database callbacks are detached only in the test harness; production behavior is unchanged.

Run studio.testing.exe --gtest_filter=WindowsTheme.* from the build directory for native palette contrast, custom color round trips, Windows theme resolution, and override persistence.

## Limits

Legacy Win32 textbox scrollbars remain OS-rendered and can appear light on dark Windows app surfaces. This change does not replace their interaction or use undocumented OS dark-mode ordinals. Native popup borders and system dialogs can likewise follow the Windows control theme. The offline designer test validates the real Table Editor layout, not database operations, a live server session, or every plugin dialog. No SQL or database behavior is changed.

## Verified results (2026-10-27)

The full x64 Debug solution build with OutDir=bin/x64/ThemeValidation completed with 0 errors and 39 warnings. All six WindowsTheme native tests passed. The expanded managed smoke suite passed, including the real Table Editor dark/light/dark renders and selected tree row rendering. Dark and light screenshots were visually inspected. Build output is in build/theme-build.log and smoke output in build/theme-smoke.log (local generated artifacts).

## Live editor opening regression

The initial designer-only test omitted the live editor Apply/Revert buttons, which use FlatStyle.System. Changing System to Flat inside HandleCreated destroyed the HWND before CreateWindowEx completed, causing Win32Exception in TabDocument.Show. Runtime style changes could also unregister native observers while GNColorsChanged still held their pointers. ControlTheme now queues all HWND-recreating style changes after creation/notification dispatch, coalesces pending work, and rejects callbacks for destroyed or replaced handles.

The new regression test reproduced the original Win32Exception before the fix. After the fix, it verifies opening an embedded document with native Button, CheckBox and RadioButton controls, dark/light switching, stable handles during notification dispatch, and disposal before queued work runs. The WBControls x64 Debug build and the complete managed theme smoke suite pass. Validation uses an isolated output directory because the running Debug application locks its DLLs.

## Connection dialog theme and layout regression

The native connection dialog used system tab headers, which ignored the shared dark palette. MformsStandardTabControl now uses the existing FlatTabControl renderer with no close buttons or reordering. All themed top-level forms also apply the same Windows dark-title-bar setting as MainForm, including light/theme/high-contrast transitions.

MformsTextBox cached its original black foreground and restored it on focus. It now tracks normal foreground changes while excluding temporary placeholder colors. Single-line native entries honor preferred-height limits and explicit minimums, and update the height limit when the font changes. Native table/box layouts now enforce control minimum/maximum sizes before DeferWindowPos, which otherwise bypasses WinForms constraints. A regression with a taller help row reproduced field stretching before this fix.

The native table/box layout engines kept construction-time sizes for non-filling labels and buttons, even after fonts changed. They now account for current preferred text sizes. Text buttons no longer lose space to the image-button padding adjustment. Font, text, and size-constraint changes trigger layout; wrapped-label measurements are invalidated before their setters can trigger it.

Run ./testing/windows/RunThemeControlsSmoke.ps1 -OutputDirectory ./bin/x64/ConnectionValidation -TestName ConnectionDialogSmoke. This uses the actual native bridge text field, label, table, box layout, button, and standard-tab classes in an offline dialog fixture. Only the box's native backend background-image painter is substituted, since there is no live backend in the fixture. The test reproduced black focused text, clipped labels, and system tabs before the fix. It verifies readable focus colors, complete captions after text/font changes, label height, non-overlapping buttons, tab selection, dark/light/dark switching, and the DWM title-bar attribute when supported. It writes connection-dialog-dark.png and connection-dialog-light.png; these renders include the shared controls but do not reproduce the complete database-backed connection wizard. DrawToBitmap uses legacy non-client rendering, so the title-bar setting is checked through DWM rather than screenshot pixels.

Validation logs: build/connection-dialog-before.log, build/connection-dialog-after.log, build/connection-theme-regression.log, build/connection-scale-regression.log, and build/connection-startup-regression.log. Existing startup, scaling, and theme smoke suites pass. The running application is not replaced by the isolated validation build.

Final connection-dialog validation: the complete x64 Debug solution build finished with 0 errors and 6 warnings (build/connection-dialog-final-build.log). ConnectionDialogSmoke, ThemeControlsSmoke, UiScaleSmoke, and MainFormStartupSmoke pass against the final binaries. The updated dialog-control dark/light renders were inspected; single-line field stretching was reproduced separately in build/connection-field-height-before.log and is now covered by the passing suite.
