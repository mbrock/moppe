# Mac releases and updates

Tags `v*` build all three desktop platforms and publish a GitHub release only
when their jobs succeed. The Mac job builds an Apple Silicon app with embedded
assets and land, signs it with Developer ID and hardened runtime, submits it
to Apple, staples the accepted ticket, then tests a moved ZIP with Gatekeeper,
signature/ticket validation, CLI help, and an invalid numeric argument. Hosted
CI does not promise a Metal rendering device.

The app uses Sparkle 2.10.1 with its standard native update UI. Its application
menu has **Check for Updates…** and **Automatically Check for Updates**. Checks
are enabled by default; automatic installation is disabled. Install and
Relaunch first asks the game to quit through its regular loop, destroys the
game, and only then allows Sparkle to replace and relaunch the app. Updates
replace the bundle; user caches and data remain outside it.

CMake development builds keep Sparkle optional. To build it locally:

```sh
python3 scripts/sparkle-release.py fetch build/sparkle
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMOPPE_AUTO_UPDATES=ON -DMOPPE_SPARKLE_ROOT="$PWD/build/sparkle"
cmake --build build --target moppe
LUV_MACOS_SIGN_IDENTITY='Developer ID Application: Your Name (TEAM)' \
LUV_MACOS_NOTARY_PROFILE=luv-release \
  python3 scripts/macos-sign-release.py build/moppe.app
python3 scripts/test-macos-release.py build/moppe-macos-arm64.zip
```

The shared packaging tools use the same `LUV_MACOS_*` environment variables as
Luvcraft. The signing script covers all Mach-O executables and nested Sparkle
bundles inside-out; it never uses `codesign --deep` for signing. No JIT
entitlement is added to Moppe. The CI keychain imports Apple's Developer ID
intermediates and signs/runs a probe before building the game.

GitHub Actions secrets are configured in `mbrock/moppe`:
`MACOS_CERTIFICATE_P12_BASE64`, `MACOS_CERTIFICATE_PASSWORD`,
`MACOS_SIGN_IDENTITY`, `APPLE_ID`, `APPLE_TEAM_ID`,
`APPLE_APP_SPECIFIC_PASSWORD`, and `SPARKLE_PRIVATE_KEY`.
The Developer ID identity is shared with Luvcraft; its certificate expires on
2027-02-01 and needs replacement for future signing before then.

Sparkle has a separate key for Moppe. Its public key and local Keychain account
are in `deploy/macos/sparkle.json`. Back up the private key securely with
`generate_keys --account se.brockman.moppe.updates -x FILE` from the pinned
Sparkle distribution. Never commit the exported file. The private update key
is stored in the Keychain and the repository's Actions secret.

`MOPPE_VERSION` comes from the tag and `MOPPE_BUILD_NUMBER` from the increasing
workflow run number. After the stapled ZIP passes testing, `generate_appcast`
signs the archive and update feed. The app requires a signed feed and archive
verification before extraction. Both are GitHub release assets; the stable
feed is `https://github.com/mbrock/moppe/releases/latest/download/appcast.xml`,
while each enclosure names its immutable release tag. Existing versions without
Sparkle need one manual upgrade. Future stable releases appear automatically.

The workflow's **macos_only** input qualifies the Mac pipeline without building
Linux and Windows; manual runs retain artifacts and do not publish a release.

Local qualification exercised the actual Sparkle UI with isolated bundles and
a signed loopback feed: checking, downloading, verifying, extracting, Install
and Relaunch, replacement by the next build, a new game process, and normal
quit afterward. SDL's NSApplication subclass intercepts termination; the bridge
restores Cocoa's termination implementation only after game teardown so the
installer can finish without bypassing game persistence.
