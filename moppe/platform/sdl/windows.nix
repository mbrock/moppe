# The game for 64-bit Windows 10 and later, cross-built on Linux: NHAL on
# Vulkan in an SDL3 window: one moppe.exe with its assets and the default
# world's land built in (MOPPE_EMBED_ASSETS), alone and zipped with the
# licences.
# Zig's Clang 21 targets MinGW-w64 on the Universal CRT and links libc++,
# SDL3, and volk statically, so the executable imports only Windows' own
# DLLs; volk opens the GPU driver's vulkan-1.dll at run time.
{ pkgs, luv-shaderc }:
let
  sources = import ./sources.nix { inherit pkgs; };
  lands = import ../../game/lands.nix { inherit pkgs; };
  zig = pkgs.zig_0_16;
  # x86-64-v3 (Haswell and later) as on Linux: hardware FMA for CORE-MATH,
  # whose results are the same bits either way.
  toolchain = pkgs.runCommand "moppe-windows-toolchain" { } ''
    mkdir -p $out/bin
    for language in cc c++; do
      cat > $out/bin/$language <<SCRIPT
    #!${pkgs.runtimeShell}
    exec ${zig}/bin/zig $language -target x86_64-windows-gnu -mcpu=x86_64_v3 "\$@"
    SCRIPT
    done
    for tool in ar ranlib rc; do
      cat > $out/bin/$tool <<SCRIPT
    #!${pkgs.runtimeShell}
    exec ${zig}/bin/zig $tool "\$@"
    SCRIPT
    done
    chmod +x $out/bin/*
  '';
  # Zig builds libc++, the C library, and compiler-rt for the target on
  # first use, with pages of warnings that would garble CMake's reading of
  # the compiler's output, and again for each set of link flags that names
  # them differently: a stripped link (-s, the game's) and SDL's AVX-512
  # probe each cost a minute. Built once here, each build starts from a
  # copy instead.
  zigCache = pkgs.runCommand "moppe-windows-zig-cache" { } ''
    export ZIG_GLOBAL_CACHE_DIR=$out ZIG_LOCAL_CACHE_DIR=$TMPDIR/local
    echo 'int main () {}' > warm.cc
    ${toolchain}/bin/c++ warm.cc -o warm.exe 2> /dev/null
    ${toolchain}/bin/c++ -s warm.cc -o warm.exe 2> /dev/null
    rm -rf $out/tmp
  '';
  common = {
    dontFixup = true;
    nativeBuildInputs = [
      pkgs.cmake
      pkgs.ninja
    ];
    preConfigure = ''
      cp -r ${zigCache} $TMPDIR/zig-cache
      chmod -R u+w $TMPDIR/zig-cache
      export ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zig-cache
      export ZIG_LOCAL_CACHE_DIR=$TMPDIR/zig-local-cache
      export CFLAGS="-ffile-prefix-map=/nix/store/=/dependencies/"
      export CXXFLAGS="$CFLAGS"
    '';
    cmakeFlags = [
      "-DCMAKE_SYSTEM_NAME=Windows"
      "-DCMAKE_SYSTEM_PROCESSOR=x86_64"
      "-DCMAKE_C_COMPILER=${toolchain}/bin/cc"
      "-DCMAKE_CXX_COMPILER=${toolchain}/bin/c++"
      "-DCMAKE_RC_COMPILER=${toolchain}/bin/rc"
      "-DCMAKE_AR=${toolchain}/bin/ar"
      "-DCMAKE_RANLIB=${toolchain}/bin/ranlib"
      "-DCMAKE_BUILD_TYPE=Release"
    ];
  };
  volk = pkgs.stdenvNoCC.mkDerivation (
    common
    // {
      pname = "moppe-windows-volk";
      inherit (pkgs.vulkan-volk) version src;
      buildInputs = [ pkgs.vulkan-headers ];
      cmakeFlags = common.cmakeFlags ++ [ "-DVOLK_INSTALL=ON" ];
    }
  );
  game = pkgs.stdenvNoCC.mkDerivation (
    common
    // {
      pname = "moppe-windows-game";
      version = "0.1.0";
      inherit (sources) src;
      nativeBuildInputs = common.nativeBuildInputs ++ [
        luv-shaderc
        pkgs.python3
        pkgs.zstd
      ];
      buildInputs = [
        volk
        pkgs.vulkan-headers
      ];
      cmakeFlags = common.cmakeFlags ++ [
        "-DBUILD_TESTING=OFF"
        "-DMOPPE_WINDOWS_DESKTOP=ON"
        "-DMOPPE_VULKAN_DYNAMIC=ON"
        "-DCMAKE_EXE_LINKER_FLAGS=-s"
        "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
        "-DFETCHCONTENT_SOURCE_DIR_MP-UNITS=${sources.mp-units}"
        # SDL3 is built into the game from nixpkgs' copy of its source.
        "-DFETCHCONTENT_SOURCE_DIR_SDL3=${pkgs.sdl3.src}"
        # Zig's Clang has AVX-512; probing it links a whole runtime anew.
        "-DCOMPILER_SUPPORTS_AVX512F=ON"
        "-DMOPPE_LANDS_DIR=${lands}"
        "-DMOPPE_EMBED_ASSETS=ON"
      ];
    }
  );
in
pkgs.runCommand "moppe-windows"
  {
    nativeBuildInputs = [
      pkgs.binutils
      pkgs.zip
    ];
    passthru = { inherit game; };
  }
  ''
    mkdir -p staging/moppe/licenses $out
    cp ${game}/bin/moppe.exe staging/moppe/
    chmod -R u+w staging
    cp ${pkgs.sdl3.src}/LICENSE.txt staging/moppe/licenses/SDL.txt
    cp ${pkgs.vulkan-volk.src}/LICENSE.md staging/moppe/licenses/volk.txt
    cp ${sources.mp-units}/LICENSE.md staging/moppe/licenses/mp-units.txt
    cp ${sources.src}/third_party/box3d/LICENSE staging/moppe/licenses/box3d.txt
    cp ${sources.src}/third_party/core-math/LICENSE staging/moppe/licenses/core-math.txt
    cp ${sources.src}/third_party/nanoarrow/LICENSE.txt staging/moppe/licenses/nanoarrow.txt
    cp ${sources.src}/third_party/nanoarrow/FLATCC-LICENSE staging/moppe/licenses/flatcc.txt
    for library in libcxx libcxxabi libunwind; do
      cp ${zig}/lib/zig/$library/LICENSE.TXT staging/moppe/licenses/$library.txt
    done
    cp ${zig}/lib/zig/libc/mingw/COPYING staging/moppe/licenses/mingw-w64.txt
    # Refuse a package that would need a DLL Windows does not have.
    objdump -p staging/moppe/moppe.exe | sed -n 's/.*DLL Name: //p' \
      > $out/imports.txt
    if grep -viE '^(kernel32|user32|gdi32|shell32|ole32|oleaut32|advapi32|winmm|imm32|setupapi|version|api-ms-win-crt-.*)\.dll$' \
        $out/imports.txt; then
      echo "moppe.exe imports DLLs Windows does not provide" >&2
      exit 1
    fi
    cd staging
    find moppe -exec touch -d @1 {} +
    zip -qrX $out/moppe-windows-x86_64.zip moppe
    cp moppe/moppe.exe $out/moppe-windows-x86_64.exe
  ''
