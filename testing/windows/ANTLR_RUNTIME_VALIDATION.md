# Windows ANTLR runtime ABI

ANTLR and SqlStudio exchange STL objects across a DLL boundary. In particular,
`intervalToString` in `mysql_parser_module.cpp` destroys the `std::string` returned
by `Vocabulary::getDisplayName`. Both modules must use the same shared CRT and
iterator-debug setting.

The October 2026 crash was reproduced with these settings:

| Module | CRT | `_ITERATOR_DEBUG_LEVEL` |
| --- | --- | --- |
| `db.mysql.parser.grt.dll` | `/MDd` (shared Debug) | 2 |
| Old `antlr4-runtime.dll` | `/MTd` (static Debug) | 2 |
| Fixed `antlr4-runtime.dll` | `/MDd` (shared Debug) | 2 |

The iterator levels already matched. The old ANTLR DLL imported only
`KERNEL32.dll`, and its `Vocabulary.obj` recorded
`RuntimeLibrary=MTd_StaticDebug`. The parser object recorded
`RuntimeLibrary=MDd_DynamicDebug`. The separate CRT instances caused debug heap
assertions when the caller freed ANTLR's string proxy.

`build/windows/build_3rdparty.ps1` now passes `-DWITH_STATIC_CRT=OFF` for both
configurations: `/MDd` in Debug and `/MD` in Release. An ANTLR-specific stamp
revision invalidates old cached builds without rebuilding unrelated dependencies.
No application iterator-debug overrides are needed.

## Rebuild and check

Stop the SqlStudio debugging session first: Visual Studio can hold ANTLR's
original build PDB open even when the loaded DLL is a staged copy.

From the repository root, with `MSS_3DPARTY_PATH` pointing to the bundle:

```powershell
.\build\windows\build_3rdparty.ps1 -Only antlr4-runtime -BuildOnly
.\PrepareOutputDir.ps1 . Debug x64
.\testing\windows\RunAntlrRuntimeSmoke.ps1 -Configuration Debug
.\testing\windows\RunAntlrRuntimeSmoke.ps1 -Configuration Release
.\testing\windows\RunAntlrRuntimeSmoke.ps1 -Configuration Debug -RuntimeDirectory .\bin\x64\Debug
```

Build the solution in Visual Studio before restarting SqlStudio. For a Release
application, also run `PrepareOutputDir.ps1 . Release x64`.

The smoke test compiles a consumer with the application's CRT settings and
exercises numeric, literal, and long symbolic vocabulary names, plus interval
vectors. Strings are created in the runtime DLL and destroyed by the consumer;
vocabulary construction also transfers allocations in the opposite direction.
Debug CRT errors fail the test immediately without opening an assertion dialog.

Validation on 2026-10-28: the old Debug DLL reproduced debug-heap assertions;
rebuilt and staged Debug and Release DLLs passed. The fixed Debug DLL imports
the shared Debug CRT, and its hash matches the bundle copy. This test isolates
the failing ABI boundary; it does not exercise the entire SQL editor UI. A second
dependency build correctly reused the new shared-CRT stamp.
The complete x64 Debug solution build also succeeded (existing compiler and
missing GoogleTest PDB warnings remain).
