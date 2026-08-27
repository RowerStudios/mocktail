// GLES version-pin shim for the system-egl (OpenGL) backend.
//
// This is loaded in place of the host libEGL.so as the guest's libEGL.so
// (exact_adapter). It links against the real libEGL so every EGL call forwards
// transparently, while overriding eglCreateContext and eglGetProcAddress to
// force the OpenGL ES major/minor version requested via
// MOCKTAIL_SYSTEM_GLES_VERSION (30/31/32). This catches both the direct symbol
// path and the eglGetProcAddress path that the guest may use.

#include <EGL/egl.h>

#include <dlfcn.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef EGL_CONTEXT_MINOR_VERSION
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#endif

namespace {

int g_requested_minor = -1;  // -1 means "auto" (pass through untouched).

using EglCreateContextFn = EGLContext (*)(EGLDisplay, EGLConfig, EGLContext,
                                          const EGLint*);
using EglGetProcAddressFn = decltype(&::eglGetProcAddress);

EglCreateContextFn g_real_create_context = nullptr;
EglGetProcAddressFn g_real_get_proc_address = nullptr;

void ResolveReal() {
  if (g_real_create_context != nullptr) {
    return;
  }
  void* real_egl = ::dlopen("libEGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
  if (real_egl == nullptr) {
    real_egl = ::dlopen("libEGL.so.1", RTLD_LAZY);
  }
  if (real_egl == nullptr) {
    real_egl = ::dlopen("libEGL.so", RTLD_LAZY);
  }
  if (real_egl == nullptr) {
    return;
  }
  g_real_create_context =
      reinterpret_cast<EglCreateContextFn>(::dlsym(real_egl, "eglCreateContext"));
  g_real_get_proc_address = reinterpret_cast<EglGetProcAddressFn>(
      ::dlsym(real_egl, "eglGetProcAddress"));
}

}  // namespace

extern "C" {

__attribute__((constructor)) void mocktail_egl_shim_init() {
  const char* version = std::getenv("MOCKTAIL_SYSTEM_GLES_VERSION");
  if (version == nullptr || version[0] == '\0') {
    return;
  }
  const std::string v(version);
  if (v == "30" || v == "3.0") {
    g_requested_minor = 0;
  } else if (v == "31" || v == "3.1") {
    g_requested_minor = 1;
  } else if (v == "32" || v == "3.2") {
    g_requested_minor = 2;
  }
  ResolveReal();
}

EGLContext eglCreateContext(EGLDisplay display, EGLConfig config,
                            EGLContext share_context,
                            const EGLint* attribs) {
  if (g_requested_minor < 0 || g_real_create_context == nullptr) {
    return g_real_create_context == nullptr
               ? EGL_NO_CONTEXT
               : g_real_create_context(display, config, share_context, attribs);
  }

  std::vector<EGLint> attrs;
  bool saw_major = false;
  bool saw_minor = false;
  for (const EGLint* p = attribs; p != nullptr && *p != EGL_NONE; p += 2) {
    const EGLint key = p[0];
    const EGLint value = p[1];
    if (key == EGL_CONTEXT_CLIENT_VERSION) {
      attrs.push_back(key);
      attrs.push_back(3);  // Force OpenGL ES 3.x.
      saw_major = true;
    } else if (key == EGL_CONTEXT_MINOR_VERSION) {
      attrs.push_back(key);
      attrs.push_back(g_requested_minor);
      saw_minor = true;
    } else {
      attrs.push_back(key);
      attrs.push_back(value);
    }
  }
  if (!saw_major) {
    attrs.push_back(EGL_CONTEXT_CLIENT_VERSION);
    attrs.push_back(3);
  }
  if (!saw_minor) {
    attrs.push_back(EGL_CONTEXT_MINOR_VERSION);
    attrs.push_back(g_requested_minor);
  }
  attrs.push_back(EGL_NONE);

  return g_real_create_context(display, config, share_context, attrs.data());
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY
eglGetProcAddress(const char* name) {
  if (g_requested_minor >= 0 && name != nullptr &&
      std::strcmp(name, "eglCreateContext") == 0) {
    return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
        &eglCreateContext);
  }
  if (g_real_get_proc_address != nullptr) {
    return g_real_get_proc_address(name);
  }
  ResolveReal();
  return g_real_get_proc_address == nullptr ? nullptr
                                            : g_real_get_proc_address(name);
}

}  // extern "C"
