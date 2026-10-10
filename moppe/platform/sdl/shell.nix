# The Linux development shell: Clang, CMake, and Ninja; the Vulkan loader,
# headers, and validation layers; SDL3 for the window, input, and the
# Vulkan surface; spirv-tools for checking modules; luv-shaderc, which
# lowers the NHAL renderer's Lisp shaders to SPIR-V; and, for recording the
# game (tools/record), ffmpeg to encode its frames and gamescope to run it
# without a window at a chosen size.
{ pkgs, luv-shaderc }:
pkgs.mkShell.override { stdenv = pkgs.llvmPackages_21.stdenv; } {
  packages = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.sdl3
    pkgs.vulkan-headers
    pkgs.vulkan-loader
    pkgs.vulkan-validation-layers
    pkgs.vulkan-tools
    pkgs.spirv-tools
    pkgs.ffmpeg
    pkgs.gamescope
    luv-shaderc
  ];
  # The loader finds the validation layer here when MOPPE_VULKAN_VALIDATION
  # asks for it.
  VK_ADD_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
  LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [ pkgs.vulkan-loader ];
}
