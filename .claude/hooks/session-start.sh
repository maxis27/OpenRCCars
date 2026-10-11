#!/bin/bash
# Claude Code on the web: build the data tools so rcinfo is ready.
# The full game needs Vulkan 1.4.357+ and a GPU, so only the tools-only build runs here.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-.}"
cmake -B build -G Ninja -DOPENRCCARS_TOOLS_ONLY=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
