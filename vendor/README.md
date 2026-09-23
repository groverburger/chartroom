Vendored upstream dependencies, with original licenses retained:

- Dear ImGui v1.92.6-docking, commit `7a6efe395d18d9cb37cea397f63ce0d39824dc72` (MIT): https://github.com/ocornut/imgui
- GLFW 3.4, commit `a74efa0d5628b74adc0426af4c5710e287fa7c2c` (zlib): https://github.com/glfw/glfw
- nlohmann/json 3.12.0 (MIT): https://github.com/nlohmann/json

Native networking uses system/vcpkg libcurl; browser networking uses Emscripten Fetch. GLFW is built statically from the sources above on desktop and supplied by Emscripten on web.
