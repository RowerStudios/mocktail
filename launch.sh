#!/usr/bin/env bash
# Launch Mocktail with NVIDIA-specific tuning to reduce lag spikes / stutter
# on Linux (GPU clock-scaling hitches, vsync, compositor contention).
set -e
cd "$(dirname "$0")"

# Pin the GPU clocks so PowerMizer stops ramping them up/down under varying
# load -- this is the #1 cause of periodic hitches on NVIDIA Linux.
# (SyncToVBlank is managed by the binary from graphics.vsync in config.yaml,
# so do NOT force it here.)
if command -v nvidia-settings >/dev/null 2>&1; then
  nvidia-settings -a '[gpu:0]/GPUPowerMizerMode=1' >/dev/null 2>&1 || true
fi

# Threaded GL command submission + compositor bypass at the driver level.
export __GL_THREADED_OPTIMIZATIONS=1
# Bypass the X11 compositor for our window to avoid compositor-induced hitches.
export SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR=1

export MOCKTAIL_SKIP_UPDATE_CHECK=1
export ROBLOX_LIB_PATH="$PWD/rbx_bin/libroblox.so"
export MOCKTAIL_ASSET_PATH="$PWD/rbx_bin/assets/content"
export MOCKTAIL_DEBUG_SHOW_WINDOW_BEFORE_FRAME=1

# Preload the vsync interposer so eglSwapInterval(1) requests become 0 when
# graphics.vsync=off (unlocks the frame rate past the display refresh rate).
export LD_PRELOAD="$PWD/build/libmocktail_egl_vsync_shim.so${LD_PRELOAD:+:$LD_PRELOAD}"

exec ./build/mocktail "$@"
