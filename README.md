# OGX Auto Clicker

![OGX Auto Clicker](assets/ogx_name_logo.png)

Modern purple/black Windows auto clicker written in native C++ and Win32.

OGX Auto Clicker is a lightweight desktop tool with mouse automation, keyboard automation, timed key tasks, multilingual UI, and profile-style configs.

## Features

- Native C++/Win32 app with custom GDI+ UI.
- Fixed 1260x720 window layout with Mouse, Keyboard, and Settings pages.
- Mouse page with interactive left/right click zones.
- Mouse hotkeys support keyboard keys and mouse buttons:
  - Left click
  - Right click
  - Middle click
  - XButton1
  - XButton2
- Standard, Game, and Legacy input methods.
- Fixed CPS and random CPS ranges, such as 20-30 CPS.
- Direct numeric editing for CPS and timed-task seconds.
- Keyboard page with main-key mode or timed-only mode.
- Timed keyboard tasks with interval preview in hours, minutes, and seconds.
- Config management:
  - Create config
  - Update config
  - Delete config
  - Switch configs
  - Toggle auto-save
- Languages:
  - English
  - Turkish
  - Russian
  - German
  - French
  - Spanish

## Build

Open `OGXAutoClicker.sln` in Visual Studio and build `Release|x64`.

The project currently targets the installed MSVC `v145` toolset. If your Visual Studio installation uses another toolset, retarget the project from Visual Studio.

Developer PowerShell:

```powershell
msbuild .\OGXAutoClicker.sln /p:Configuration=Release /p:Platform=x64
```

Output:

```text
bin\Release\OGXAutoClicker.exe
```

## Usage

1. Open the app.
2. Choose `Mouse`, `Keyboard`, or `Settings` from the sidebar.
3. Click a keybind field and press a keyboard key or mouse button.
4. For numeric fields, click the value, type a number, then press `Enter`.
5. Use Settings to create or update configs.

## Data Locations

App settings:

```text
%LOCALAPPDATA%\OGX Auto Clicker\settings.ini
```

Configs:

```text
%LOCALAPPDATA%\OGX Auto Clicker\configs
```

## Contact

For questions, suggestions, bug reports, or feature requests, feel free to contact me on Discord.

**Discord:** ozi.coding

## Notes

The `Game` input method uses scan-code keyboard events and short mouse press timing. Some games or anti-cheat systems may block synthetic input; this project does not bypass anti-cheat protection.
