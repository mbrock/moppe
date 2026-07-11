.PHONY: all archive phone terrain-lab-shot testflight

# Configure (if needed) and build everything for macOS.
all:
	@[ -f build/build.ninja ] || cmake -B build -G Ninja
	cmake --build build

# Produce a signed App Store archive without uploading it.
archive:
	./tools/testflight archive

# Build, install, and launch Moppe on the paired iPhone.
phone:
	./tools/install-ios

# Build a fast deterministic Terrain Lab preview, capture it, and exit.
terrain-lab-shot:
	./tools/capture-terrain-lab

# Archive and upload a new build to App Store Connect for TestFlight.
testflight:
	./tools/testflight upload
