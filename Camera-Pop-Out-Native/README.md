# Camera Pop-Out Native — development build 0.2.0

This is native C++ OBS plugin source for 64-bit Windows. **This archive is source, not an installable DLL.** The Windows build and live OBS integration have not yet been verified. The crop calculations passed C++ tests in Linux.

The plugin creates its own Win32 window and renders a chosen OBS scene or video source using OBS's display renderer. It uses the finished scene, including backgrounds and filters, without Virtual Camera. Zoom and pan affect only the pop-out's view. It has no Python, PySide6, or external camera process.

## What is implemented

- OBS Tools menu: Camera Pop-Out Native, with an OBS scene/video-source selector.
- Borderless camera view, with Win32 resize/maximize styles kept for Windows Snap.
- Mouse wheel zoom, plain left-drag pan, Ctrl+drag native window movement, edge resizing.
- Resizing covers the window by cropping rather than stretching or adding bars.
- Right-click settings, reset crop, mirror, always on top, mouse move/resize lock, aspect sizes, minimize, save, close.
- Startup option with an optional OBS profile name; automatic shutdown with OBS.
- Persistent view and window settings stored in OBS's plugin configuration directory.

Lock blocks mouse movement and resizing; Windows keyboard layout commands can still move a locked window. The view follows the chosen named source, not your current Program output. Select the finished camera scene to include its separate background. Renaming a closed/unloaded source requires reselecting it in plugin settings.

## Build with GitHub (no compiler installation on your PC)

1. Create an empty GitHub repository, or use your existing plugin repository.
2. Extract this source ZIP. Put the **contents** of Camera-Pop-Out-Native at the repository root, including `.github/workflows/windows-build.yml`. Do not put the enclosing folder at the root. GitHub's browser upload can omit dot folders: use GitHub Desktop to commit the extracted folder so the workflow is included.
3. Push to main/master. Open the repository's **Actions → Build Windows plugin**. You can also choose **Run workflow** to run manually.
4. When the Windows job succeeds, download **Camera-Pop-Out-Native-Windows** from its Artifacts section. Extract the downloaded artifact, then extract the inner Windows ZIP.
5. Install the resulting camera-popout-native folder using the instructions below.

The workflow has been authored, but has not been run in this environment. A successful build produces the DLL; a live OBS 32.2.2 test is still required. It uses the official OBS plugin template build system. The build refuses a template SDK newer than 32.2.2. The template reference defaults to master; record a successful template commit and use that commit in Run workflow for reproducible follow-up builds.

## Changes in 0.2.0

- Added Windows GitHub Actions compilation, crop tests, packaged DLL artifact, and failure log artifact.
- Saved normal window positions use screen coordinates to avoid taskbar-offset drift; maximizing/minimizing does not overwrite the restored size.
- Source renames are captured when saving while the source remains open. The taskbar label identifies the selected source while the camera stays borderless.
- Switching to a profile outside the configured startup profile closes the window. The settings source list refreshes after collection changes.
- Pan changes save on release; source selection writes use the rendering state mutex.

Only crop calculations were compiled and tested here. Native window, rendering, Snap, and startup/shutdown changes require the Windows build and OBS smoke tests below.

## Build on Windows

1. Keep this folder under Documents or another writable folder.
2. Install **Visual Studio 2022 Build Tools**, selecting **Desktop development with C++**, MSVC x64 tools, and the Windows 11 SDK (10.0.22621).
3. Install **CMake 3.30 or newer**, adding it to PATH.
4. Double-click **BUILD.cmd**. The build script downloads the official OBS plugin template and its SDK dependencies, then compiles this source. It leaves build-log.txt for diagnosis.

The resulting DLL is placed in `package/camera-popout-native/bin/64bit/camera-popout-native.dll`. An installable ZIP is also created. No build was run on Windows in the authoring environment. API/SDK compatibility must be checked against your installed OBS version before calling this release ready.

The build uses the SDK version selected in the downloaded official template's buildspec.json; it does not silently assume that this is your installed OBS version. The project uses long-established APIs, but compatibility still requires a load/render test. A template revision can be pinned with `build-windows.ps1 -TemplateRef <commit>`.

## Install after a successful build

Close OBS. Copy the **camera-popout-native** folder from **package** into:

`C:/ProgramData/obs-studio/plugins/`

The final file should be:

`C:/ProgramData/obs-studio/plugins/camera-popout-native/bin/64bit/camera-popout-native.dll`

For portable/custom OBS installs, use that installation's documented plugin location. In OBS Tools → Scripts, remove or disable both old Camera Pop-Out scripts to avoid duplicate windows. Restart OBS, then choose Tools → Camera Pop-Out Native, select your finished camera scene, and click Apply and open.

## Verify on your OBS installation

- Select the finished camera scene: the added background and blur should match OBS.
- Zoom with the wheel and pan with plain drag. Check your main OBS scene remains framed as before.
- Resize narrow and wide: framing should crop, with no image stretching.
- Ctrl+drag to a monitor edge: check the Windows Snap preview and other-window suggestions. Also test Win+Z and Win+Arrow.
- Move between monitors with different scaling settings.
- Close and restart OBS: verify auto-open, saved crop/layout, and automatic close.
- Switch source and scene collection: verify no crashes and that cameras release on close.

Leave Always on top off while testing Snap Assist. Windows Settings → System → Multitasking → Snap windows controls its suggestions. Snap Assist lets you choose a second window; it does not choose one automatically.

The rendering path is an SDR window view. HDR/color-space parity and performance have not been measured. Avoid a camera scene that Display Captures this pop-out itself, which would produce recursive video.

## Recovery

If OBS fails to load or crashes after installation, close OBS and remove only `camera-popout-native` from the plugin directory, then restart OBS. No existing scene configuration is rewritten by this plugin.
