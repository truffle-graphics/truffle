# OpenGL ES Backend

On Linux with EGL/GLES development packages, this target creates a surfaceless
EGL ES 3 context and pbuffer and verifies exact clear/readback output before
exposing an adapter. Android/native-window work remains `source_only`.

The current adapter is a narrow `native_smoke` foundation with no advertised
resource, shader, pipeline, or presentation capabilities. It never substitutes
Null's logical behavior.
