# VS Code build and debug

## Windows

- Install the Microsoft C# extension (`ms-dotnettools.csharp`) for the C# debugger, or Microsoft C/C++ (`ms-vscode.cpptools`) for the native C++ debugger.
- Install Visual Studio 2022 or newer with MSBuild, Desktop development with C++, C++/CLI tools, a Windows SDK, and the .NET Framework 4.8 targeting pack.
- Provide the third-party bundle through `MSS_3DPARTY_PATH` or one of the locations supported by `build/compile-studio.ps1`: `../wb_build/bundle`, `../bundle`, or `./bundle`.
- Run **Terminal > Run Build Task > Build SqlStudio Windows (Debug)** (or **Release**). These tasks use the existing PowerShell wrapper to discover and invoke MSBuild for x64. A Developer Command Prompt is not required.
- Select **SqlStudio (C#, Windows)** or **SqlStudio (C++, Windows)** in Run and Debug, then press **F5**. Each profile builds Debug first and launches `bin/x64/Debug/SqlStudio.exe` with its output directory as the working directory. The profiles debug managed C# and native C++ respectively; they do not provide simultaneous mixed-mode debugging.

The existing Linux task remains the default. On Windows, use **Tasks: Configure Default Build Task** to select **Build SqlStudio Windows (Debug)** for **Ctrl+Shift+B**.

## Linux

Use **Build SqlStudio Linux** and **SqlStudio (GDB, Linux)** with the existing CMake build and installation directories.
