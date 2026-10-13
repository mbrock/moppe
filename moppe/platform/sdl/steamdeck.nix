# A relocatable Linux build. Only the build machine needs Nix; runtime
# libraries (including Vulkan and the display/input stack) come from the host.
{ pkgs, luv-shaderc, terrain ? null, cpu ? "x86_64_v3", archiveName ? "moppe-steamdeck" }:
let
  sources = import ./sources.nix { inherit pkgs; };
  lands = if terrain == null then import ../../game/lands.nix { inherit pkgs; } else terrain;
  # Sniper is based on Debian 11 and ships glibc 2.31. Zig supplies its
  # versioned libc ABI and static libc++/compiler runtime, including C++23
  # and Clang 21's #embed support, without a Nix glibc sysroot.
  compiler = pkgs.runCommand "moppe-steamdeck-compiler" { } ''
    mkdir -p $out/bin
    for language in cc c++; do
      cat > $out/bin/$language <<SCRIPT
    #!${pkgs.runtimeShell}
    exec ${pkgs.zig_0_16}/bin/zig $language -target x86_64-linux-gnu.2.31 -mcpu=${cpu} "\$@"
    SCRIPT
      chmod +x $out/bin/$language
    done
  '';
  common = {
    dontFixup = true;
    nativeBuildInputs = [
      pkgs.cmake
      pkgs.ninja
      pkgs.pkg-config
    ];
    AR = "${pkgs.binutils}/bin/ar";
    RANLIB = "${pkgs.binutils}/bin/ranlib";
    STRIP = "${pkgs.binutils}/bin/strip";
    CC = "${compiler}/bin/cc";
    CXX = "${compiler}/bin/c++";
    preConfigure = ''
      # Do not consume NIX_LDFLAGS: its runtime paths belong to the build host.
      export CFLAGS="-ffile-prefix-map=/nix/store/=/dependencies/"
      export CXXFLAGS="$CFLAGS"
      export ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zig-cache
      export ZIG_LOCAL_CACHE_DIR=$TMPDIR/zig-local-cache
    '';
  };
  sdl = pkgs.stdenvNoCC.mkDerivation (
    common
    // {
      pname = "moppe-steamdeck-sdl";
      inherit (pkgs.sdl3) version src;
      buildInputs = with pkgs; [
        libx11
        libxcb
        libxcursor
        libxext
        libxfixes
        libxi
        libxrandr
        libxrender
        libxscrnsaver
        libxtst
        libxkbcommon
        wayland
        wayland-protocols
        libglvnd
        alsa-lib
        libpulseaudio
        systemdLibs
      ];
      nativeBuildInputs = common.nativeBuildInputs ++ [ pkgs.wayland-scanner ];
      preConfigure = common.preConfigure + ''
        # SDL's X11 probes include headers from several propagated packages;
        # an unwrapped compiler does not add those implicitly.
        dependencyCflags=$(pkg-config --static --cflags x11 xcb xext xi xrandr xrender xcursor xfixes xscrnsaver xtst xkbcommon wayland-client wayland-egl wayland-cursor egl alsa libpulse libudev)
        export CFLAGS="$CFLAGS $dependencyCflags"
      '';
      cmakeFlags = [
        "-DCMAKE_BUILD_TYPE=Release"
        "-DSDL_SHARED=OFF"
        "-DSDL_STATIC=ON"
        "-DSDL_TEST_LIBRARY=OFF"
        "-DSDL_TESTS=OFF"
        "-DSDL_EXAMPLES=OFF"
        "-DSDL_INSTALL=ON"
        "-DSDL_DEPS_SHARED=ON"
        "-DSDL_X11=ON"
        "-DSDL_WAYLAND=ON"
        "-DSDL_WAYLAND_LIBDECOR=OFF"
        "-DSDL_KMSDRM=OFF"
        "-DSDL_OPENGL=OFF"
        "-DSDL_OPENGLES=OFF"
        "-DSDL_PIPEWIRE=OFF"
        "-DSDL_JACK=OFF"
        "-DSDL_SNDIO=OFF"
        "-DSDL_DBUS=OFF"
        "-DSDL_IBUS=OFF"
        "-DSDL_HIDAPI_LIBUSB=OFF"
        "-DSDL_RENDER_GPU=OFF"
        "-DSDL_GPU=OFF"
        "-DSDL_VULKAN=ON"
      ];
    }
  );
  volk = pkgs.stdenvNoCC.mkDerivation (
    common
    // {
      pname = "moppe-steamdeck-volk";
      inherit (pkgs.vulkan-volk) version src;
      buildInputs = [ pkgs.vulkan-headers ];
      cmakeFlags = [
        "-DVOLK_INSTALL=ON"
        "-DCMAKE_BUILD_TYPE=Release"
      ];
    }
  );
  game = pkgs.stdenvNoCC.mkDerivation (
    common
    // {
      pname = "moppe-steamdeck-game";
      version = "0.1.0";
      inherit (sources) src;
      nativeBuildInputs = common.nativeBuildInputs ++ [ luv-shaderc ];
      buildInputs = [
        sdl
        volk
        pkgs.vulkan-headers
      ];
      cmakeFlags = [
        "-DCMAKE_BUILD_TYPE=Release"
        "-DBUILD_TESTING=OFF"
        "-DCMAKE_SKIP_RPATH=ON"
        "-DMOPPE_VULKAN_DYNAMIC=ON"
        "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
        "-DFETCHCONTENT_SOURCE_DIR_MP-UNITS=${sources.mp-units}"
        "-DMOPPE_LANDS_DIR=${lands}"
      ];
      postInstall = ''
        rm -rf $out/include $out/lib
      '';
    }
  );
in
pkgs.runCommand archiveName
  {
    nativeBuildInputs = [
      pkgs.binutils
      pkgs.python3
    ];
    passthru = { inherit game sdl; };
  }
  ''
    mkdir -p staging/moppe $out
    cp -r ${game}/bin ${game}/share staging/moppe/
    chmod -R u+w staging
    strip staging/moppe/bin/moppe
    install -m755 ${./steamdeck-launcher} staging/moppe/moppe
    mkdir -p staging/moppe/licenses
    cp ${pkgs.sdl3.src}/LICENSE.txt staging/moppe/licenses/SDL.txt
    cp ${pkgs.vulkan-volk.src}/LICENSE.md staging/moppe/licenses/volk.txt
    cp ${sources.mp-units}/LICENSE.md staging/moppe/licenses/mp-units.txt
    cp ${sources.src}/third_party/box3d/LICENSE staging/moppe/licenses/box3d.txt
    cp ${sources.src}/third_party/core-math/LICENSE staging/moppe/licenses/core-math.txt
    cp ${sources.src}/third_party/nanoarrow/LICENSE.txt staging/moppe/licenses/nanoarrow.txt
    cp ${sources.src}/third_party/nanoarrow/FLATCC-LICENSE staging/moppe/licenses/flatcc.txt
    for library in libcxx libcxxabi libunwind; do
      cp ${pkgs.zig_0_16}/lib/zig/$library/LICENSE.TXT staging/moppe/licenses/$library.txt
    done
    python3 ${./verify-steamdeck.py} staging/moppe/bin/moppe > $out/verification.txt
    tar --sort=name --mtime=@1 --owner=0 --group=0 --numeric-owner \
      -czf $out/${archiveName}.tar.gz -C staging moppe
  ''
