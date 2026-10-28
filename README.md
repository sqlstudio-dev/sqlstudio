# SQL Studio

**A community-driven, open-source graphical database management tool - evolved from the foundations of MySQL Workbench.**

> ⚠️ **Release Notice:** SqlStudio is released **on demand**. There is no fixed release schedule. New builds are published when significant features, stability improvements, or security fixes are ready. Watch this repository for release notifications.

---

## 💖 Support this project

*If you find SqlStudio useful, consider supporting its development - it helps keep the project active and maintained.*

[![Donate with PayPal](https://www.paypalobjects.com/en_US/i/btn/btn_donate_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=TZP3JJYLQ2Z8A)

---

## About

SqlStudio is a free, open-source visual database design, administration, and development tool for the MySQL and MariaDB database ecosystems. It is based on the original architecture and codebase of **MySQL Workbench**, now independently developed and maintained under the **SqlStudio** project.

SqlStudio was created out of the need for a modern, community-maintained alternative to the original MySQL Workbench - actively developed, not just patched. It incorporates all the core capabilities of its predecessor while adding new features, improved stability on modern operating systems, and a long-term commitment to open development.

SqlStudio is brought to you by a team of independent contributors and database professionals who believe that a high-quality, open-source graphical MySQL/MariaDB tool should continue to exist and improve - independent of any corporate release timeline.

### About the Author

SqlStudio is led by a developer with over **25 years of experience in the software industry**. Prior to starting this project, he served as **Technical Lead for MySQL Workbench at Oracle** for more than **10 years** - making Workbench one of his key products and areas of deep expertise. Having been at the core of Workbench's architecture and development for over a decade, he knows the codebase inside out - and SqlStudio is the result of that experience, rebuilt with a fresh perspective and a long-term community focus in mind.

---

## Key Features

### SQL Development
Create and manage connections to MySQL and MariaDB database servers. The built-in SQL Editor supports syntax highlighting, auto-completion, query execution with result set inspection, explain plan visualization, and query history management.

### Data Modeling (Design)
Design and visualize your database schema using the integrated Entity-Relationship (ER) diagram editor. Perform forward engineering (model to database), reverse engineering (database to model), and schema synchronization between a model and a live server. The comprehensive Table Editor covers columns, indexes, foreign keys, triggers, partitioning, options, inserts, and stored routines.

### Server Administration
Administer MySQL and MariaDB server instances directly from the UI. Manage user accounts and privileges, configure server options, perform backup and restore operations, inspect audit logs, and monitor server health and performance in real time.

### Data Migration
Migrate schemas and data from external RDBMS platforms to MySQL or MariaDB, including:
- Microsoft SQL Server
- Microsoft Access
- Sybase ASE
- SQLite
- SQL Anywhere
- PostgreSQL
- Earlier versions of MySQL / MariaDB

The migration wizard handles schema mapping, data type conversion, and data transfer with detailed reporting at every step.

### Performance Monitoring
Built-in dashboards provide real-time performance metrics, query analysis, index efficiency reporting, and InnoDB status visualization - helping you identify and resolve bottlenecks quickly.

---

## Supported Server Versions

SqlStudio is tested and supported with:
- **MySQL** 5.7, 8.0, 8.1, 8.2, 8.3, 8.4 (LTS), 9.x
- **MariaDB** 10.4, 10.5, 10.6 (LTS), 10.11 (LTS), 11.x (support will be soon published)

---

## Supported Platforms

| Platform | Status |
|---|---|
| Windows 10 / 11 (x64) | ✅ Supported |
| Windows Server 2019 / 2022 | ✅ Supported |
| Ubuntu 20.04 / 22.04 / 24.04 | ✅ Supported |
| Debian 11 / 12 | ✅ Supported |
| macOS 12 Monterey and later (ARM & Intel) | ✅ Supported |
| Fedora / RHEL 8+ | 🔄 Community builds |

---

## Release Model

SqlStudio follows a **release-on-demand** policy:

- Releases are made when they are ready - not on a fixed calendar schedule.
- Each release is tagged and published in the [Releases](../../releases) section of this repository.
- Release notes are included with every tagged version.
- Pre-release and development builds may be available on the `develop` branch but are not guaranteed to be stable.

To be notified of new releases, **watch this repository** and select *"Releases only"* in GitHub notification settings.

---

## Building from Source

Prerequisites and build instructions are provided in the [BUILDING.md](BUILDING.md) file. The project supports CMake-based builds on all platforms.

A Windows build requires Visual Studio 2022 or later with the Desktop C++ workload. On Linux and macOS, GCC 11+ or Clang 14+ is required.

On Windows, run `powershell -NoProfile -ExecutionPolicy Bypass -File .\build\compile-studio.ps1`
to build the `SqlStudio` application in Debug/x64. The script discovers the newest
Visual Studio or Build Tools installation with C++ tools, selects its installed
toolset, and locates the third-party bundle through `MSS_3DPARTY_PATH`,
`../wb_build/bundle`, `../bundle`, or `./bundle`. Use `-BundleDir <path>` or
`-VsInstallPath <path>` to override discovery, `-Configuration Release` for a
release build, and `-DryRun` to inspect the selected tools without compiling.
The C++/CLI tools, Windows SDK, and .NET Framework 4.8 targeting pack are also required.

On Debian and Ubuntu, a `.deb` package is built the same way as in CI, on the
distribution it is meant for:

```sh
sudo build/linux/install_deps.sh
build/linux/build_3rdparty.sh --bundle "$HOME/sqlstudio-bundle"
export WB_BUNDLE_DIR="$HOME/sqlstudio-bundle"
build/compile.sh --build-type Release --prefix /usr --antlr-jar /path/to/antlr-4.13.2-complete.jar
DESTDIR="$PWD/stage" cmake --install build/output
build/linux/build_pyodbc.sh --bundle "$HOME/sqlstudio-bundle" --sdist pyodbc-5.3.0.tar.gz --out stage/usr/lib/sql-studio
build/linux/make_deb.sh --stage stage --out dist
```

The checks that run on every pull request are described in
[CONTRIBUTING.md](CONTRIBUTING.md#build-and-package-checks).

In VS Code, **Ctrl+Shift+B** runs the default **Build SqlStudio (Debug)** task.
The Release task is available under **Terminal > Run Task**. Compiler diagnostics
appear in the Problems panel. Build output goes to `bin/x64/Debug` or `bin/x64/Release`.

---

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting a pull request. For bugs and feature requests, open an issue using the provided templates.

Pull requests should target the `develop` branch. Direct commits to `main` are reserved for release merges.

---

## License

SqlStudio is released under the **GNU General Public License, version 2.0**. See the [License](License.txt) file for full terms.

This distribution may include materials developed by third parties. For license and attribution notices for these materials, please refer to the [License](License.txt) file.

---

## Acknowledgements

SqlStudio is built on the foundation of MySQL Workbench, originally developed by the MySQL team at Oracle. We gratefully acknowledge the work of all past and present contributors to that project.

---

## Links

- 📘 Documentation: *(coming soon - published with first stable release)*
- 🐛 Issue Tracker: [GitHub Issues](https://github.com/sqlstudio-dev/sqlstudio/issues)
- 💬 Discussions: [GitHub Discussions](https://github.com/sqlstudio-dev/sqlstudio/discussions)
- 🎧 Discord Channel: [Join the Discord server](https://discord.gg/gQNPbpgeg)
- 🔖 Releases: [GitHub Releases](https://github.com/sqlstudio-dev/releases)
- 🌐 Source repository: [Source repo](https://github.com/sqlstudio-dev/sqlstudio)
