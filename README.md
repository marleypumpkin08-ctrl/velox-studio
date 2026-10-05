# Velox Studio

Velox Studio is a native Windows desktop image-editor project. Its rendering
backend is Vulkan; the application does not use a browser or web application
shell. The workspace currently provides a GPU-rendered canvas, a basic brush,
rectangular selection, layer visibility and creation, stroke undo/redo, native
`.vlx` project save/load, and autosave/recovery. The native Qt shell also
includes update checking and a separate Windows updater.

The canvas currently renders checkerboard transparency, brush-stroke geometry,
and imported raster layers through Vulkan. Raster painting into image buffers,
the remaining blend modes, and broader editing tools are not yet implemented.

Image files can be imported as raster layers and are uploaded to Vulkan textures
for GPU compositing. Normal, Multiply, and Screen blend modes are currently
rendered on the GPU. Other saved blend modes remain available in project
metadata, but their layer controls are disabled and their preview falls back to
Normal until those compositing paths are implemented.

Use **File → Import Image as Layer** (Ctrl+Shift+I) to add PNG, JPEG, BMP, TIFF,
or WebP images up to 8192 × 8192 pixels.

Use the Brush tool to paint, Rectangle Select to constrain subsequent brush
strokes, and **Edit → Undo/Redo** (Ctrl+Z/Ctrl+Y) for stroke history. Projects
are saved as `.vlx`; modified documents are periodically autosaved to the
user-local recovery directory and offered on the next launch.

## Windows build prerequisites

- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake and Ninja (the Visual Studio CMake components are supported)
- Qt 6.8.3 for MSVC 2022
- The LunarG Vulkan SDK and a Vulkan-capable graphics driver

The user-scoped Qt and Vulkan SDK installs can be bootstrapped with:

```powershell
.\scripts\bootstrap-deps.ps1
```

## Configure, build, and run

From the repository root, load the toolchain environment and build:

```powershell
. .\scripts\env.ps1
& $env:VELOX_CMAKE -S . -B $env:VELOX_BUILD_DIR -G Ninja `
    -DCMAKE_MAKE_PROGRAM="$env:VELOX_NINJA" `
    -DQt6_DIR="$env:VELOX_QT_DIR\lib\cmake\Qt6" `
    -DVulkan_ROOT="$env:VULKAN_SDK"
& $env:VELOX_CMAKE --build $env:VELOX_BUILD_DIR --config Debug
& "$env:VELOX_BUILD_DIR\bin\VeloxStudio.exe"
```

The application creates a Vulkan graphics pipeline and renders the canvas into
the Vulkan swap chain. A Vulkan loader and compatible graphics driver are
required at runtime. Project documents use the versioned `.vlx` format.

## Beta releases

Download `VeloxStudio-windows-x64.zip` from the GitHub Releases page and extract
it to a writable folder. Run `VeloxStudio.exe` from the extracted folder. A
Vulkan-capable graphics driver must be installed; the Vulkan SDK is needed to
build the application but is not bundled in the release.

The updater accepts a staged ZIP containing the deployed `VeloxStudio.exe` and
related runtime files; release assets must be named
`VeloxStudio-windows-x64.zip` and include GitHub's SHA-256 asset digest. Updates
replace the application folder only after Velox Studio exits and then restart it.
The updater checks releases in `marleypumpkin08-ctrl/velox-studio`; beta releases
may need to be installed manually because GitHub's stable-latest endpoint
excludes prereleases.
