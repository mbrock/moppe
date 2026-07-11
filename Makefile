.PHONY: all archive callgraph check-format complexity format hooks phone profile \
	terrain-lab-shot testflight xcode

# Configure (if needed) and build everything for macOS.
all:
	@[ -f build/build.ninja ] || cmake -B build -G Ninja
	cmake --build build

# Format all tracked C, C++, Objective-C, and Metal sources.
format:
	./tools/format

# Measure per-function cyclomatic and cognitive complexity.
complexity:
	./tools/complexity-report

# Extract the static C++ call graph as CSV data.
callgraph:
	./tools/callgraph-report

# Check formatting without changing files (also used by the Git hook).
check-format:
	./tools/format --check

# Opt this checkout into the repository's Git hooks.
hooks:
	git config core.hooksPath tools/git-hooks

# Generate and open the macOS Xcode workspace.
xcode:
	cmake --preset xcode
	open moppe.xcworkspace

# Build, then record a Metal System Trace (with CPU stacks) while you
# play; quitting the game ends the recording and opens Instruments.
profile:
	./tools/profile

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
