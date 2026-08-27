// Host system OpenGL/EGL capability probe for the system-egl (OpenGL) backend.

#include "mocktail/graphics/system_egl_probe.h"

#include <dlfcn.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

namespace mocktail {
namespace graphics {
namespace {

// EGL_PLATFORM_SURFACELESS_MESA from eglext.h, copied so the probe does not
// depend on a Mesa extension header being present.
constexpr EGLenum kEglPlatformSurfacelessMesa = 0x31BE;

constexpr const char* kDefaultEglLibrary = "libEGL.so.1";
constexpr const char* kDefaultGlesLibrary = "libGLESv2.so.2";

constexpr std::uint32_t kGlRenderer = 0x1F01;  // GL_RENDERER
constexpr std::uint32_t kGlVendor = 0x1F00;    // GL_VENDOR

BackendCapability Unavailable(std::string detail) {
  return {GraphicsBackendKind::kSystemEgl, CapabilityState::kUnavailable,
          HardwareAcceleration::kUnknown, std::move(detail)};
}

std::string DlErrorFor(const char* kind, const std::string& path) {
  std::string detail = "failed to load ";
  detail += kind;
  detail += " library '";
  detail += path;
  detail += "': ";
  const char* error = dlerror();
  detail += error != nullptr ? error : "unknown dynamic loader error";
  return detail;
}

bool HasSymbols(void* handle,
                const std::initializer_list<const char*>& symbols,
                std::string* missing_symbol) {
  for (const char* symbol : symbols) {
    if (::dlsym(handle, symbol) == nullptr) {
      if (missing_symbol != nullptr) {
        *missing_symbol = symbol;
      }
      return false;
    }
  }
  return true;
}

bool IsSoftwareRenderer(const char* renderer, const char* vendor) {
  auto contains = [](const char* haystack, const char* needle) {
    if (haystack == nullptr || needle == nullptr) {
      return false;
    }
    const char* found = std::strstr(haystack, needle);
    return found != nullptr;
  };
  auto lower_contains = [&](const char* source, const char* token) {
    if (source == nullptr) {
      return false;
    }
    std::string lowered;
    lowered.reserve(std::strlen(source) + 1);
    for (const char* p = source; *p != '\0'; ++p) {
      lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    return std::strstr(lowered.c_str(), token) != nullptr;
  };
  (void)contains;
  return lower_contains(renderer, "llvmpipe") ||
         lower_contains(renderer, "swiftshader") ||
         lower_contains(renderer, "softpipe") ||
         lower_contains(renderer, "software") ||
         lower_contains(vendor, "software") ||
         lower_contains(renderer, "lavapipe");
}

}  // namespace

BackendCapability ProbeSystemEgl(const SystemEglProbeOptions& options) {
  const std::string egl_path =
      options.egl_library_path.empty() ? kDefaultEglLibrary : options.egl_library_path;
  const std::string gles_path = options.gles_library_path.empty()
                                    ? kDefaultGlesLibrary
                                    : options.gles_library_path;

  dlerror();
  void* egl_handle = ::dlopen(egl_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (egl_handle == nullptr) {
    return Unavailable(DlErrorFor("EGL", egl_path));
  }
  dlerror();
  void* gles_handle = ::dlopen(gles_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (gles_handle == nullptr) {
    const std::string detail = DlErrorFor("GLES", gles_path);
    ::dlclose(egl_handle);
    return Unavailable(detail);
  }

  std::string missing;
  if (!HasSymbols(egl_handle,
                  {"eglGetProcAddress", "eglInitialize", "eglTerminate",
                   "eglQueryString", "eglChooseConfig", "eglCreateContext",
                   "eglCreatePbufferSurface", "eglMakeCurrent",
                   "eglSwapBuffers", "eglGetDisplay"},
                  &missing)) {
    ::dlclose(gles_handle);
    ::dlclose(egl_handle);
    return Unavailable("system EGL library is missing required symbol " + missing);
  }

  auto get_proc_address =
      reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(::dlsym(egl_handle, "eglGetProcAddress"));
  auto get_platform_display =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
          ::dlsym(egl_handle, "eglGetPlatformDisplayEXT"));
  if (get_platform_display == nullptr && get_proc_address != nullptr) {
    get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        get_proc_address("eglGetPlatformDisplayEXT"));
  }
  auto get_display =
      reinterpret_cast<PFNEGLGETDISPLAYPROC>(::dlsym(egl_handle, "eglGetDisplay"));
  auto initialize =
      reinterpret_cast<PFNEGLINITIALIZEPROC>(::dlsym(egl_handle, "eglInitialize"));
  auto terminate =
      reinterpret_cast<PFNEGLTERMINATEPROC>(::dlsym(egl_handle, "eglTerminate"));
  auto query_string =
      reinterpret_cast<PFNEGLQUERYSTRINGPROC>(::dlsym(egl_handle, "eglQueryString"));
  auto choose_config =
      reinterpret_cast<PFNEGLCHOOSECONFIGPROC>(::dlsym(egl_handle, "eglChooseConfig"));
  auto create_context =
      reinterpret_cast<PFNEGLCREATECONTEXTPROC>(::dlsym(egl_handle, "eglCreateContext"));
  auto create_pbuffer =
      reinterpret_cast<PFNEGLCREATEPBUFFERSURFACEPROC>(
          ::dlsym(egl_handle, "eglCreatePbufferSurface"));
  auto make_current =
      reinterpret_cast<PFNEGLMAKECURRENTPROC>(::dlsym(egl_handle, "eglMakeCurrent"));
  auto destroy_surface =
      reinterpret_cast<PFNEGLDESTROYSURFACEPROC>(::dlsym(egl_handle, "eglDestroySurface"));
  auto destroy_context =
      reinterpret_cast<PFNEGLDESTROYCONTEXTPROC>(::dlsym(egl_handle, "eglDestroyContext"));

  EGLDisplay display = EGL_NO_DISPLAY;
  if (get_platform_display != nullptr) {
    const EGLint surfaceless[] = {EGL_NONE};
    display = get_platform_display(kEglPlatformSurfacelessMesa,
                                   EGL_DEFAULT_DISPLAY, surfaceless);
  }
  if (display == EGL_NO_DISPLAY && get_display != nullptr) {
    display = get_display(EGL_DEFAULT_DISPLAY);
  }
  if (display == EGL_NO_DISPLAY) {
    ::dlclose(gles_handle);
    ::dlclose(egl_handle);
    return Unavailable("system EGL could not create a display");
  }

  EGLint major = 0;
  EGLint minor = 0;
  if (initialize(display, &major, &minor) != EGL_TRUE) {
    terminate(display);
    ::dlclose(gles_handle);
    ::dlclose(egl_handle);
    return Unavailable("system EGL display failed eglInitialize");
  }

  const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_NONE,
  };
  EGLConfig config = nullptr;
  EGLint config_count = 0;
  if (choose_config(display, config_attributes, &config, 1, &config_count) != EGL_TRUE ||
      config_count == 0) {
    terminate(display);
    ::dlclose(gles_handle);
    ::dlclose(egl_handle);
    return Unavailable("system EGL has no OpenGL ES 3 capable configuration");
  }

  const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context =
      create_context(display, config, EGL_NO_CONTEXT, context_attributes);
  if (context == EGL_NO_CONTEXT) {
    terminate(display);
    ::dlclose(gles_handle);
    ::dlclose(egl_handle);
    return Unavailable("system EGL could not create an OpenGL ES 3 context");
  }

  const EGLint pbuffer_attributes[] = {EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
  EGLSurface surface = create_pbuffer(display, config, pbuffer_attributes);
  if (surface == EGL_NO_SURFACE ||
      make_current(display, surface, surface, context) != EGL_TRUE) {
    destroy_context(display, context);
    terminate(display);
    ::dlclose(gles_handle);
    ::dlclose(egl_handle);
    return Unavailable("system EGL could not make the OpenGL ES context current");
  }

  using GlGetStringFn = const char* (*)(std::uint32_t);
  GlGetStringFn gl_get_string = nullptr;
  if (get_proc_address != nullptr) {
    gl_get_string = reinterpret_cast<GlGetStringFn>(get_proc_address("glGetString"));
  }
  const char* renderer_raw =
      gl_get_string != nullptr ? gl_get_string(kGlRenderer) : nullptr;
  const char* vendor_raw =
      gl_get_string != nullptr ? gl_get_string(kGlVendor) : nullptr;
  // Copy the strings: they are only valid while the context is current.
  const std::string renderer = renderer_raw != nullptr ? renderer_raw : "";
  const std::string vendor = vendor_raw != nullptr ? vendor_raw : "";

  // The GL_VENDOR/GL_RENDERER strings are only valid while the context is
  // current, so classify acceleration before tearing the display down.
  const bool software = IsSoftwareRenderer(renderer.c_str(), vendor.c_str());

  make_current(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  destroy_surface(display, surface);
  destroy_context(display, context);
  terminate(display);
  ::dlclose(gles_handle);
  ::dlclose(egl_handle);

  if (software && !options.allow_software_device) {
    std::string detail = "system EGL resolved to a software rasterizer";
    if (!renderer.empty()) {
      detail += " (renderer=";
      detail += renderer;
      detail += ")";
    }
    return {GraphicsBackendKind::kSystemEgl, CapabilityState::kLoadable,
            HardwareAcceleration::kSoftware, std::move(detail)};
  }

  std::string detail = "validated system EGL ";
  detail += std::to_string(major);
  detail += ".";
  detail += std::to_string(minor);
  if (!vendor.empty()) {
    detail += ", vendor=";
    detail += vendor;
  }
  if (!renderer.empty()) {
    detail += ", renderer=";
    detail += renderer;
  }
  detail += software ? ", software" : ", hardware";

  return {GraphicsBackendKind::kSystemEgl, CapabilityState::kReady,
          software ? HardwareAcceleration::kSoftware : HardwareAcceleration::kHardware,
          std::move(detail)};
}

}  // namespace graphics
}  // namespace mocktail
