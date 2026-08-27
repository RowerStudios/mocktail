// Loads the host system EGL/OpenGL ES drivers for the system-egl (OpenGL)
// backend and exposes their entry points to the Bionic guest.

#include "mocktail/graphics/system_egl_bridge.h"

#include <EGL/egl.h>
#include <dlfcn.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <string>
#include <vector>

namespace mocktail {
namespace graphics {
namespace {

constexpr const char* kDefaultEglLibrary = "libEGL.so.1";
constexpr const char* kDefaultGlesLibrary = "libGLESv2.so.2";

constexpr const char* kEglExportNames[] = {
    "eglBindAPI",
    "eglChooseConfig",
    "eglCreateContext",
    "eglCreatePbufferSurface",
    "eglCreateWindowSurface",
    "eglDestroyContext",
    "eglDestroySurface",
    "eglGetConfigAttrib",
    "eglGetCurrentContext",
    "eglGetCurrentDisplay",
    "eglGetCurrentSurface",
    "eglGetDisplay",
    "eglGetError",
    "eglGetProcAddress",
    "eglInitialize",
    "eglMakeCurrent",
    "eglQueryString",
    "eglQuerySurface",
    "eglSwapBuffers",
    "eglSwapInterval",
    "eglTerminate",
};

constexpr const char* kGlesExportNames[] = {
    "glActiveTexture",
    "glAttachShader",
    "glBindAttribLocation",
    "glBindBuffer",
    "glBindFramebuffer",
    "glBindTexture",
    "glBlendFunc",
    "glClear",
    "glClearColor",
    "glCompileShader",
    "glCreateProgram",
    "glCreateShader",
    "glDeleteBuffers",
    "glDeleteProgram",
    "glDeleteShader",
    "glDeleteTextures",
    "glDrawElements",
    "glEnable",
    "glEnableVertexAttribArray",
    "glGetAttribLocation",
    "glGetError",
    "glGetIntegerv",
    "glGetProgramiv",
    "glGetShaderiv",
    "glGetString",
    "glGetUniformLocation",
    "glLinkProgram",
    "glShaderSource",
    "glTexImage2D",
    "glTexParameteri",
    "glUniform1f",
    "glUniform1i",
    "glUniformMatrix4fv",
    "glUseProgram",
    "glVertexAttribPointer",
    "glViewport",
};

}  // namespace

namespace {

using EglCreateContextFn = EGLContext (*)(EGLDisplay, EGLConfig, EGLContext,
                                         const EGLint*);

#ifndef EGL_CONTEXT_MINOR_VERSION
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#endif

// Configured by SystemEglBridge::Load from MOCKTAIL_SYSTEM_GLES_VERSION.
// 0 = auto (let the driver and Roblox negotiate); 30/31/32 force GLES 3.0/3.1/3.2.
int g_requested_gles_version = 0;
EglCreateContextFn g_real_egl_create_context = nullptr;

EGLContext WrapEglCreateContext(EGLDisplay display, EGLConfig config,
                                EGLContext share_context,
                                const EGLint* attribs) {
  if (g_real_egl_create_context == nullptr) {
    return EGL_NO_CONTEXT;
  }
  if (g_requested_gles_version == 0) {
    // No pin requested: behave exactly like the host driver.
    return g_real_egl_create_context(display, config, share_context, attribs);
  }
  const EGLint requested_major = 3;
  const EGLint requested_minor = (g_requested_gles_version == 32) ? 2
                                 : (g_requested_gles_version == 31) ? 1
                                 : 0;
  std::vector<EGLint> attrs;
  bool saw_major = false;
  bool saw_minor = false;
  for (const EGLint* p = attribs; p != nullptr && *p != EGL_NONE; p += 2) {
    const EGLint key = p[0];
    const EGLint value = p[1];
    if (key == EGL_CONTEXT_CLIENT_VERSION) {
      attrs.push_back(key);
      attrs.push_back(requested_major);
      saw_major = true;
    } else if (key == EGL_CONTEXT_MINOR_VERSION) {
      attrs.push_back(key);
      attrs.push_back(requested_minor);
      saw_minor = true;
    } else {
      attrs.push_back(key);
      attrs.push_back(value);
    }
  }
  if (!saw_major) {
    attrs.push_back(EGL_CONTEXT_CLIENT_VERSION);
    attrs.push_back(requested_major);
  }
  if (!saw_minor) {
    attrs.push_back(EGL_CONTEXT_MINOR_VERSION);
    attrs.push_back(requested_minor);
  }
  attrs.push_back(EGL_NONE);
  return g_real_egl_create_context(display, config, share_context,
                                  attrs.data());
}

}  // namespace

bool SystemEglBridge::Load() {
  if (handle_ != nullptr) {
    return true;
  }

  exports_.clear();
  error_.clear();
  egl_library_path_.clear();
  gles_library_path_.clear();

  const char* egl_override = std::getenv("MOCKTAIL_SYSTEM_EGL_LIBRARY");
  const char* gles_override = std::getenv("MOCKTAIL_SYSTEM_GLES_LIBRARY");
  egl_library_path_ =
      egl_override != nullptr && egl_override[0] != '\0' ? egl_override : kDefaultEglLibrary;
  gles_library_path_ = gles_override != nullptr && gles_override[0] != '\0'
                           ? gles_override
                           : kDefaultGlesLibrary;

  dlerror();
  void* egl_handle = ::dlopen(egl_library_path_.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (egl_handle == nullptr) {
    const char* loader_error = ::dlerror();
    error_ = "could not load system EGL library '" + egl_library_path_ + "'";
    if (loader_error != nullptr) {
      error_ += ": ";
      error_ += loader_error;
    }
    return false;
  }

  dlerror();
  void* gles_handle = ::dlopen(gles_library_path_.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (gles_handle == nullptr) {
    const char* loader_error = ::dlerror();
    error_ = "could not load system GLES library '" + gles_library_path_ + "'";
    if (loader_error != nullptr) {
      error_ += ": ";
      error_ += loader_error;
    }
    ::dlclose(egl_handle);
    return false;
  }

  for (const char* name : kEglExportNames) {
    void* address = ::dlsym(egl_handle, name);
    if (address == nullptr) {
      error_ = "system EGL library is missing export '";
      error_ += name;
      error_ += "'";
      ::dlclose(gles_handle);
      ::dlclose(egl_handle);
      exports_.clear();
      return false;
    }
    if (std::strcmp(name, "eglCreateContext") == 0) {
      g_real_egl_create_context =
          reinterpret_cast<EglCreateContextFn>(address);
    }
    exports_.emplace(name, address);
  }

  for (const char* name : kGlesExportNames) {
    void* address = ::dlsym(gles_handle, name);
    if (address == nullptr) {
      error_ = "system GLES library is missing export '";
      error_ += name;
      error_ += "'";
      ::dlclose(gles_handle);
      ::dlclose(egl_handle);
      exports_.clear();
      return false;
    }
    exports_.emplace(name, address);
  }

  const char* gles_version_env = std::getenv("MOCKTAIL_SYSTEM_GLES_VERSION");
  if (gles_version_env != nullptr && gles_version_env[0] != '\0') {
    const std::string version_text(gles_version_env);
    if (version_text == "30" || version_text == "3.0") {
      g_requested_gles_version = 30;
    } else if (version_text == "31" || version_text == "3.1") {
      g_requested_gles_version = 31;
    } else if (version_text == "32" || version_text == "3.2") {
      g_requested_gles_version = 32;
    }
  }
  if (g_requested_gles_version != 0 && g_real_egl_create_context != nullptr) {
    // Interpose eglCreateContext so Roblox's GLES version request is pinned.
    exports_["eglCreateContext"] =
        reinterpret_cast<void*>(&WrapEglCreateContext);
  }

  handle_ = egl_handle;
  gles_handle_ = gles_handle;

  // When a version pin is requested, load the GLES-version shim and expose it
  // as the guest's libEGL.so. The shim wraps eglCreateContext/eglGetProcAddress
  // so the pin is honoured regardless of how the guest resolves the symbols
  // (direct import or eglGetProcAddress). Other EGL calls forward unchanged.
  if (g_requested_gles_version != 0) {
    const std::string shim_path = ShimLibraryPath();
    ::dlerror();
    void* shim = ::dlopen(shim_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (shim != nullptr) {
      handle_ = shim;
      std::cerr << "[gles-pin] using version shim " << shim_path << '\n';
    } else {
      const char* load_error = ::dlerror();
      std::cerr << "[gles-pin] could not load version shim '" << shim_path
                << "': " << (load_error != nullptr ? load_error : "unknown")
                << "; falling back to synthetic-library wrapper\n";
    }
  }

  return true;
}

std::string SystemEglBridge::ShimLibraryPath() {
  char buffer[PATH_MAX];
  const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length > 0) {
    buffer[length] = '\0';
    const std::string exe(buffer, static_cast<size_t>(length));
    const auto slash = exe.find_last_of('/');
    const std::string dir =
        slash == std::string::npos ? "." : exe.substr(0, slash);
    return dir + "/libmocktail_egl_shim.so";
  }
  return "libmocktail_egl_shim.so";
}

void* SystemEglBridge::Find(const char* name) const {
  const auto it = exports_.find(name);
  return it != exports_.end() ? it->second : nullptr;
}

}  // namespace graphics
}  // namespace mocktail
