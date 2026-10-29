#pragma once

// Selects the native GL headers and the GL feature tiers shared by the
// EGL (Linux desktop GL 4.5 / GLES 3.0) and CGL (macOS GL 4.1 core) contexts.

#if defined(TRUFFLE_GL_CONTEXT_CGL)
#if !defined(TRUFFLE_GL_API_DESKTOP) || defined(TRUFFLE_GL_API_ES)
#error "the CGL context only provides the desktop GL core profile"
#endif
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION 1
#endif
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#else
#ifndef TRUFFLE_GL_CONTEXT_EGL
#define TRUFFLE_GL_CONTEXT_EGL 1
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined(TRUFFLE_GL_API_DESKTOP)
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#elif defined(TRUFFLE_GL_API_ES)
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#else
#error "a GL API profile must be selected"
#endif
#endif

// EGL desktop contexts request GL 4.5 core. CGL is capped at GL 4.1 core,
// which lacks compute, storage blocks, immutable storage, texture views,
// base-instance draws, memory barriers and KHR_debug.
#if defined(TRUFFLE_GL_CONTEXT_EGL)
#define TRUFFLE_GL_HAS_TEXTURE_STORAGE 1
#define TRUFFLE_GL_HAS_DEBUG_OUTPUT 1
#if defined(TRUFFLE_GL_API_DESKTOP)
#define TRUFFLE_GL_HAS_COMPUTE 1
#define TRUFFLE_GL_HAS_TEXTURE_VIEWS 1
#define TRUFFLE_GL_HAS_BASE_INSTANCE 1
#endif
#endif
