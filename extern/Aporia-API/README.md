# Aporia Engine — API

> **This branch contains only the public API headers (`include/Aporia`). There is no engine code here.**
>
> To get the engine, switch to the branch you need:
>
> ```
> git clone -b main https://gitlab.com/aporia-xyz/aporia-engine.git
> ```
>
> or, in an existing clone:
>
> ```
> git checkout main
> ```

| Branch | Backend | Contents |
| --- | --- | --- |
| `main` | Vulkan | Stable engine, release line |
| `experimental/dev` | Vulkan | Development line, experimental features |
| `directx` | D3D12 | DirectX backend (work in progress) |
| `api` | — | Public API headers and tools (this branch) |

## Tools

Everything below runs locally; no CI and no network beyond `git clone` / `git pull`.

| Command | What it does |
| --- | --- |
| `tools\build.bat [branch]` | Clones the branch into `..\Aporia-sandbox\<branch>`, syncs the API headers, configures, builds and runs `ctest`. Default branch: `main`. |
| `tools\create_project.bat <name> [ref]` | Creates a new project folder with the engine in `engine\`, a ready `CMakeLists.txt` and `src\main.cpp`, then builds it. Default ref: `main`. |

Examples:

```bat
tools\build.bat
tools\build.bat experimental/dev
tools\create_project.bat my_game
```

## Using the API in an engine branch

Each engine branch keeps this branch's headers in `extern\Aporia-API\` and adds one line to its CMake:

```cmake
target_include_directories(AporiaEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/Aporia-API/include)
```

To update the headers in an engine branch, run `tools\sync-api.bat` from the `C:\Aporia-engine` root (or copy `include\Aporia` into `extern\Aporia-API\include\Aporia` manually).

**Rule:** public headers are edited here only. Engine branches never edit `extern\Aporia-API` directly.

## Version

`APORIA_API_VERSION_*` macros live in `include/Aporia/Core/Version.hpp`.
