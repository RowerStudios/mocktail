#include <string>

#include <gtest/gtest.h>

#include "mocktail/graphics/graphics_backend.h"
#include "mocktail/graphics/system_egl_bridge.h"
#include "mocktail/graphics/system_egl_probe.h"

namespace mocktail::graphics {
namespace {

TEST(SystemEglBridgeTest, LoadsHostSystemLibraries) {
  SystemEglBridge bridge;
  ASSERT_TRUE(bridge.Load()) << bridge.error();
  EXPECT_FALSE(bridge.egl_library_path().empty());
  EXPECT_FALSE(bridge.gles_library_path().empty());
  // 21 EGL exports + 36 GLES exports.
  EXPECT_EQ(bridge.exports().size(), 57u);

  EXPECT_NE(bridge.Find("eglCreateWindowSurface"), nullptr);
  EXPECT_NE(bridge.Find("eglSwapBuffers"), nullptr);
  EXPECT_NE(bridge.Find("glDrawElements"), nullptr);
  EXPECT_NE(bridge.Find("glGetString"), nullptr);
}

TEST(SystemEglProbeTest, ValidatesHostSystemEgl) {
  SystemEglBridge bridge;
  ASSERT_TRUE(bridge.Load()) << bridge.error();

  const BackendCapability capability = ProbeSystemEgl(SystemEglProbeOptions{});
  EXPECT_EQ(capability.backend, GraphicsBackendKind::kSystemEgl);
  // The system stack must at least initialize; it is either ready (hardware or
  // allowed software) or loadable (software, disallowed by default policy).
  ASSERT_NE(capability.state, CapabilityState::kUnavailable)
      << capability.detail;
  EXPECT_FALSE(capability.detail.empty());
}

TEST(SystemEglProbeTest, SoftwareIsLoadableWhenDisallowed) {
  SystemEglProbeOptions options;
  options.allow_software_device = false;
  const BackendCapability capability = ProbeSystemEgl(options);
  if (capability.acceleration == HardwareAcceleration::kSoftware) {
    EXPECT_EQ(capability.state, CapabilityState::kLoadable);
  }
}

TEST(SystemEglProbeTest, SelectableThroughBackendPolicy) {
  const BackendCapability system_egl = ProbeSystemEgl(SystemEglProbeOptions{});
  if (system_egl.state == CapabilityState::kUnavailable) {
    GTEST_SKIP() << "host system EGL unavailable: " << system_egl.detail;
  }

  const std::vector<BackendCapability> capabilities = {system_egl};
  BackendSelectionPolicy policy;
  policy.requested = GraphicsBackendKind::kSystemEgl;
  policy.allow_fallback = false;
  policy.require_hardware_acceleration = false;
  policy.require_runtime_validation = false;

  const BackendSelection selection =
      SelectGraphicsBackend(policy, capabilities);
  ASSERT_TRUE(selection.status.ok()) << selection.detail;
  EXPECT_EQ(selection.backend, GraphicsBackendKind::kSystemEgl);
}

}  // namespace
}  // namespace mocktail::graphics
