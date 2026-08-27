#ifndef MOCKTAIL_GRAPHICS_SYSTEM_EGL_BRIDGE_H_
#define MOCKTAIL_GRAPHICS_SYSTEM_EGL_BRIDGE_H_

#include <string>
#include <unordered_map>

#include <dlfcn.h>

namespace mocktail {
namespace graphics {

// Loads the host system EGL and OpenGL ES drivers and exposes their entry
// points to the Bionic guest when the system-egl (OpenGL) backend is selected.
//
// This is the routing counterpart of BionicEglBridge: instead of handing the
// Android client Mocktail's own libEGL.so shim, the loader resolver points the
// guest's libEGL.so/libGLESv2.so synthetic symbols at the resolved addresses
// from the host system libraries so Roblox renders through the native GL stack
// (Mesa, NVIDIA, etc.) instead of ANGLE.
class SystemEglBridge final {
 public:
  bool Load();
  bool loaded() const { return handle_ != nullptr; }
  bool IsLoaded() const { return handle_ != nullptr; }
  void* handle() const { return handle_; }
  void* gles_handle() const { return gles_handle_; }
  const std::string& error() const { return error_; }
  const std::string& egl_library_path() const { return egl_library_path_; }
  const std::string& gles_library_path() const { return gles_library_path_; }
  const std::unordered_map<std::string, void*>& exports() const {
    return exports_;
  }
  void* Find(const char* name) const;

  // Path to the GLES-version shim (libmocktail_egl_shim.so) next to the
  // running executable.
  static std::string ShimLibraryPath();

 private:
  void* handle_ = nullptr;
  void* gles_handle_ = nullptr;
  std::string error_;
  std::string egl_library_path_;
  std::string gles_library_path_;
  std::unordered_map<std::string, void*> exports_;
};

}  // namespace graphics
}  // namespace mocktail

#endif  // MOCKTAIL_GRAPHICS_SYSTEM_EGL_BRIDGE_H_
