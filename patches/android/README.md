# Android renderer build-stage patch pipeline

`rt64-rgba-presentation.patch` is deliberately separate from the dependency
checkout patch manifest. `runtime-recomp/cmake/android/RendererPatches.cmake`
copies only the two relevant translation units into the Android build tree,
checks and applies this patch there, and replaces those two target sources.
It never applies the patch in `extern/rt64` or to generated guest code.

The isolated generated directory has its own Git metadata solely to make patch
application independent of where the build directory resides. No commits or
index changes are required. Each configure starts from the current dependency
sources; a context mismatch or unexpected source count fails configuration.
Windows and Linux do not include this pipeline.

Android's surface implementation guarantees RGBA8, whereas RT64's desktop
swapchain and final VI pipeline request BGRA8. Both must change together. The
patch preserves UNORM output and leaves intermediate framebuffer formats,
textures, shaders, interpolation, antialiasing and gameplay unchanged. RT64's
Vulkan inspector already derives its render-pass format from the selected
surface format, so it requires no separate format override.

Primary reference:
https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/vulkan/libvulkan/swapchain.cpp
(`GetPhysicalDeviceSurfaceFormatsKHR`: "We must support R8G8B8A8").

This repairs a source-proven Android compatibility defect. The supplied phone
log confirms device loss, but omitted startup, so this must not be described as
a device-confirmed resolution until the updated APK is tested on that phone.

## SDL2 Vulkan lifecycle (preview.4)

`sdl2-vulkan-surface-lifecycle.patch` is applied to a copy of SDL2 2.32.10's
`src/core/android/SDL_android.c` through `cmake/android/SdlPatches.cmake`.
The original downloaded SDL sources remain unchanged. The Android shared SDL
target substitutes only that translation unit and uses its original include
directory. Configuration fails on missing source/context rather than silently
skipping a fix.

The surface-change callback formerly attempted EGL surface creation without
checking whether its window was OpenGL. That contradicts the existing
Android_CreateWindow guard/comment explaining that EGL surfaces are incompatible
with attaching a Vulkan surface. The destruction callback also waited up to
500 ms for an EGL backup even for a Vulkan window. Both now require a managed
OpenGL window. Launcher OpenGL handling is retained.

The project-owned RT64 Android window adapter also scopes
`SDL_HINT_VIDEO_EXTERNAL_CONTEXT=1` to the game renderer's lifetime, restoring
the previous hint on destruction. SDL's Android event pump uses this hint to
avoid saving/restoring an OpenGL context for externally managed graphics.
Reference: https://wiki.libsdl.org/SDL2/SDL_HINT_VIDEO_EXTERNAL_CONTEXT

These fixes do not implement complete device-loss or replaced-native-surface
recovery. No device crash trace confirming the user's capture failure was
available during this pass. Do not claim universal capture/resume support.
