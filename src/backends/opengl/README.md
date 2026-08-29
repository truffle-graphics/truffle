# OpenGL Backend

On Linux with EGL/OpenGL development packages, this target creates a
surfaceless EGL display, pbuffer, and desktop OpenGL context, then performs a
deterministic clear/readback before exposing an adapter. Queue smoke restores
the context and executes `glFinish` with GL error validation.

This is a `native_smoke` matrix slice. Resource objects, shaders, pipelines,
state tracking, window surfaces, and presentation remain unsupported. Other
platforms retain an unavailable factory and stay `source_only`; shared logical
behavior remains exclusively in Null.
