#ifndef MOCKTAIL_GRAPHICS_SYSTEM_EGL_PROBE_H_
#define MOCKTAIL_GRAPHICS_SYSTEM_EGL_PROBE_H_

#include <string>

#include "mocktail/graphics/graphics_backend.h"

namespace mocktail {
namespace graphics {

// Options for probing the host system OpenGL/EGL stack. When the library paths
// are left empty the loader resolves the platform SONAMEs (libEGL.so.1 and
// libGLESv2.so.2) from the host loader search path rather than from a pinned
// ANGLE or Mocktail bridge distribution.
struct SystemEglProbeOptions {
  std::string egl_library_path;
  std::string gles_library_path;
  bool allow_software_device = false;
};

// Loads the host system EGL/GLES pair, initializes a surfaceless EGL display,
// and validates a real OpenGL ES context. No draw calls are issued. The result
// reports hardware vs software acceleration by inspecting the GL_RENDERER and
// GL_VENDOR strings (llvmpipe, swiftshader, softpipe and similar are treated as
// software rasterizers).
BackendCapability ProbeSystemEgl(const SystemEglProbeOptions& options);

}  // namespace graphics
}  // namespace mocktail

#endif  // MOCKTAIL_GRAPHICS_SYSTEM_EGL_PROBE_H_
