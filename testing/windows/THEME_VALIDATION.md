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
