# WebGL2 Backend

Emscripten builds contain a browser-native WebGL2 canvas-context path and expose
an adapter only if context creation succeeds. Repository maturity remains
`source_only` until an official headless-browser compile/smoke/validation lane
records evidence. Non-Emscripten builds return `unsupported`.
