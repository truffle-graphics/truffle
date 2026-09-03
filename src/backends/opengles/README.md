# OpenGL ES Backend

On Linux with EGL/GLES development packages, this target creates a surfaceless
EGL ES 3 context and pbuffer and verifies exact clear/readback output before
exposing an adapter. Android/native-window work remains `source_only`.

The current adapter is a narrow `native_smoke` foundation. It owns
upload/readback/device-local buffers, buffer views, mapping, native copies, and
arbitrary-range byte fills, with exact readback in the Linux native test.
Textures, shaders, pipelines, broader state tracking, Android surfaces, and
presentation remain unsupported. It never substitutes Null's logical behavior.
