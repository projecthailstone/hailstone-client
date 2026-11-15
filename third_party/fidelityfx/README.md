# AMD FidelityFX FSR 3.1 upscaler (shaders only)

From github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, tag `v1.1.4` (commit
c6efa6bf7f2027b3ec94f28578bb5965eabb9e55): the FSR upscaler 3.1.4's GLSL, MIT (LICENSE.txt).

- `gpu/`: the headers the passes include, at the SDK's paths under `sdk/include/FidelityFX/gpu/`.
- `shaders/`: the Vulkan backend's pass entry points (`sdk/src/backends/vk/shaders/fsr3upscaler/*_pass.glsl`),
  those Hailstone dispatches (no frame generation, debug view or reactive-mask generation).

The SDK's runtime and Vulkan backend are not used: `src/render/fsr3_upscaler.cpp` is Hailstone's own host side
(pipelines, descriptors, resources, dispatch order), following `sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp`.
The shaders are compiled by `cmake/Shaders.cmake` with glslc or glslangValidator (docs/graphics.md "AMD FSR 3.1").

Local changes, each marked "Hailstone" in the file:

- `gpu/fsr3upscaler/ffx_fsr3upscaler_callbacks_glsl.h`: `rw_luma_history` is declared `rgba16f` (the SDK declares
  `rgba8` while it makes the image RGBA16F), and `rw_upscaled_output` `rgba16f` (formatless in the SDK, which needs
  `shaderStorageImageWriteWithoutFormat`).
