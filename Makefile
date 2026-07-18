
.PHONY: all archive atelier callgraph callgraph-analyze callgraph-cache callgraph-diff \
	check-format \
	complexity format hooks plan plan-graph phone profile terrain-lab-shot testflight tracy tv \
	tracy-benchmark-capture tracy-capture tracy-import tree-shot water-benchmark \
	xcode

# Configure (if needed) and build everything for macOS.
all:
	@[ -f build/build.ninja ] || cmake -B build -G Ninja
	cmake --build build

# Build and open the standalone Metal graphics workshop.
atelier:
	@[ -f build/build.ninja ] || cmake -B build -G Ninja
	cmake --build build --target atelier
	open build/atelier.app

# Format all tracked C, C++, Objective-C, and Metal sources.
format:
	./tools/format

# Measure per-function cyclomatic and cognitive complexity.
complexity:
	./tools/complexity-report

# Validate the repository-native RFC and work-item dependency graph.
plan:
	./tools/plan check

# Print the current work-item graph as Mermaid Markdown.
plan-graph:
	./tools/plan graph

# Extract the static C++ call graph as CSV data.
callgraph:
	./tools/callgraph-report

# Combine call-graph centrality, communities, and source complexity.
callgraph-analyze: callgraph
	./tools/callgraph-analyze

# Compare call-graph and complexity metrics for HEAD^ and HEAD.
callgraph-diff:
	./tools/callgraph-diff

# Open the shared content-addressed analysis catalog in DuckDB.
callgraph-cache:
	./tools/callgraph-cache

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

# Build Moppe with low-overhead, on-demand Tracy instrumentation.
tracy:
	cmake --preset tracy
	cmake --build --preset tracy

# Record a finite gameplay trace to build-tracy/moppe.tracy.
tracy-capture:
	./tools/tracy-capture

# Capture a deterministic graphics-feature cube with CPU and Metal zones.
tracy-benchmark-capture:
	./tools/tracy-benchmark-capture

# Import an existing capture: make tracy-import TRACE=path/to/file.tracy
tracy-import:
	@test -n "$(TRACE)" || (echo "TRACE is required" >&2; exit 1)
	./tools/tracy-import "$(TRACE)"

# Produce a signed App Store archive without uploading it.
archive:
	./tools/testflight archive

# Build, install, and launch Moppe on the paired iPhone.
phone:
	./tools/install-ios

# Build, install, and launch Moppe on the paired Apple TV.
tv:
	./tools/install-tvos

# Build a fast deterministic Terrain Lab preview, capture it, and exit.
terrain-lab-shot:
	./tools/capture-terrain-lab

# Build a deterministic grove from surface habitat and capture it in-game.
tree-shot:
	./tools/capture-trees

# Capture the curated hydrology gallery and write its HTML/CSV report.
water-benchmark:
	./tools/water-benchmark

# Archive and upload a new build to App Store Connect for TestFlight.
testflight:
	./tools/testflight upload
