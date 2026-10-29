# SqlStudio MSI (WiX 7)

The installer uses **WiX Toolset 7.0.0**, pinned in `SqlStudio.wixproj` and
`MySQLCA/MySQLCA.csproj`. NuGet restores the SDK, extensions, and managed custom
action tooling; a global WiX installation and WiX 3 targets are not needed.
The `http://wixtoolset.org/schemas/v4/wxs` namespace is also used by WiX 7.

## Prerequisites

- Windows with the .NET 8 SDK or newer.
- .NET Framework 4.8 on the build machine and target machines. Reference
  assemblies are restored via NuGet. This matches the current Windows frontend.
- A complete staged application distribution, such as the unpacked portable
  .zip from `build/zip/make_zip.ps1` (it includes the Visual C++ runtime).
  Packaging does not build SqlStudio. Every file in the distribution is
  installed: `source/sql_studio_fragment.xml` lists only the files that need
  hand-authored entries (SqlStudio.exe with its file associations, the README
  and license) and harvests the rest at build time with a `Files` element.
- WiX 7 EULA acceptance. Review the
  [WiX OSMF terms and acceptance instructions](https://docs.firegiant.com/wix/osmf/).
  The CI workflow accepts them with `-p:AcceptEula=wix7`; the checked-in
  projects do not, so a local build needs the same property.

## Build with the SDK

From the repository root, for example:

```powershell
dotnet build build/msi/SqlStudio.wixproj -c Release `
  -p:Edition=debug -p:ProductVersion=1.0.0 `
  -p:InstallerPlatform=x64 -p:Platform=x64 `
  "-p:PayloadDirectory=D:\path\to\staged\distribution"
```

If you have reviewed and accepted the WiX terms, add `-p:AcceptEula=wix7` to the
command (including in CI), or use WiX's documented per-user acceptance mechanism.
Without acceptance, WiX reports `WIX7015`.

Supported editions are `community`, `commercial`, and `debug`; architectures are
`x86` and `x64`. Set both architecture properties to the same value so the MSI and
custom-action DLL use the same architecture. Versions use `major.minor.build`
within MSI limits (255.255.65535). The product display name retains major.minor.

The output is `build/msi/bin/<architecture>/<edition>/sql_studio.msi`. A direct
SDK build produces an unsigned MSI; the existing distribution/signing wrapper
remains available below. `MySQLCA.CA.dll` is built from source through a project
reference, replacing the prebuilt DLL from `MSS_3DPARTY_PATH`.

## Existing release wrapper

From a Visual Studio Developer Command Prompt in `build/msi`:

```bat
MakeSetup.bat community 1.0.0 win64
```

After reviewing and accepting the terms, `set WIX_ACCEPT_EULA=wix7` passes that
acceptance through NMake to the SDK build. The default payload directories remain
`bin/<architecture>/Release_OSS`, `Release`, and `Debug` for community, commercial,
and debug respectively.

The wrapper's existing optional `SIGN_USER SIGN_PASS` arguments, Oracle signing
client, distribution naming, and MD5 output are retained. They still require the
original release environment (`MSS_3DPARTY_PATH`, signing client, checksum tool,
and edition-specific license files). No signing service is invoked by an SDK build.

## Validation before release

Build with a complete payload, inspect MSI validation output, then test fresh
install, repair, uninstall, and upgrade from an existing release in a disposable
Windows VM. Verify shortcuts and Python compilation/cleanup for each supported
architecture. Existing upgrade codes, component GUIDs, UI, and payload inventory
are retained. The optional `WB_UPGRADE_CA_PATH` upgrade DLL remains externally
supplied and must match the package architecture.
