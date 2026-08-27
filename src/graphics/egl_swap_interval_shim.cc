// LD_PRELOAD interposer that neutralises application-requested vsync.
//
// Roblox (and SDL) request vsync via eglSwapInterval(1). The NVIDIA driver's
// __GL_SYNC_TO_VBLANK=0 does NOT override an explicit application request, so
// the frame rate stays capped to the display refresh rate. This library forces
// every eglSwapInterval call to 0 while MOCKTAIL_VSYNC=off, which is required
// to unlock the frame rate. When vsync is not off the call passes through
// unchanged, so the library is safe to always preload.

#include <EGL/egl.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>

typedef EGLBoolean (*PFN_eglSwapInterval)(EGLDisplay, EGLint);

extern "C" EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
  static PFN_eglSwapInterval real = nullptr;
  if (real == nullptr) {
    real = reinterpret_cast<PFN_eglSwapInterval>(
        dlsym(RTLD_NEXT, "eglSwapInterval"));
  }
  const char* vsync = std::getenv("MOCKTAIL_VSYNC");
  if (vsync != nullptr && std::strcmp(vsync, "off") == 0) {
    interval = 0;
  }
  if (real == nullptr) {
    return EGL_FALSE;
  }
  return real(dpy, interval);
}
