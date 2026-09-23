Chartroom C++ uses these third-party components:

- Dear ImGui, Copyright (c) 2014-2026 Omar Cornut; MIT. See vendor/imgui/LICENSE.txt.
- GLFW, Copyright (c) 2002-2006 Marcus Geelnard and 2006-2024 Camilla Löwy; zlib. See vendor/glfw/LICENSE.md.
- JSON for Modern C++, Copyright (c) 2013-2025 Niels Lohmann; MIT. See vendor/json/LICENSE.MIT.
- Roboto font, Copyright 2011 Google Inc. All Rights Reserved.; Apache 2.0. See assets/Roboto-LICENSE.txt. The font is embedded in the executable.
- libcurl for native HTTP; its own distribution and transitive libraries retain their respective notices. https://curl.se/docs/copyright.html
- Emscripten runtime for browser builds; MIT/UIUC. https://github.com/emscripten-core/emscripten/blob/main/LICENSE

Pinned vendored source versions are listed in vendor/README.md.

Public market-data integration is informed by [OpenTerminal](https://github.com/ErTasselli/OpenTerminal),
Copyright (c) 2026 OpenTerminal contributors, MIT. Its license is included in
[vendor/OpenTerminal-LICENSE.txt](vendor/OpenTerminal-LICENSE.txt). Chartroom uses the providers directly;
it does not require an OpenTerminal installation.
