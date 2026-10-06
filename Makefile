
.PHONY: all lavoir moppe archive atelier etalon etalon-test etalon-watch \
	callgraph callgraph-analyze callgraph-cache callgraph-diff \
	check-format \
	complexity figure figure-model format models gazetteer grass-lab hooks plan plan-graph phone profile \
	test \
	testflight tracy tv \
	tracy-benchmark-capture tracy-capture tracy-import water-benchmark \
	xbox xcode

all: moppe

BLENDER ?= /Applications/Blender.app/Contents/MacOS/Blender
FIGURE_PREVIEW ?= /tmp/hiker.png

# Re-export the hiker from models/hiker.blend and render its lineup.
figure:
	$(BLENDER) -b models/hiker.blend --python tools/figure/export.py
	$(BLENDER) -b models/hiker.blend --python tools/figure/preview.py -- \
	  $(FIGURE_PREVIEW)

# Regenerate models/hiker.blend from its script (discards hand edits).
figure-model:
	$(BLENDER) -b --factory-startup --python tools/figure/build.py

# Re-export the bike and glider from their .blend files and render their
# turnarounds beside FIGURE_PREVIEW. MODELS_REBUILD=1 first regenerates
# the .blend files from their scripts (discarding hand edits).
models:
	for m in bike glider; do \
	  if [ -n "$(MODELS_REBUILD)" ]; then \
	    $(BLENDER) -b --factory-startup --python tools/figure/$$m.py; fi; \
	  $(BLENDER) -b models/$$m.blend --python tools/figure/export_model.py \
	    -- $${m}_model && \
	  $(BLENDER) -b models/$$m.blend --python tools/figure/preview_model.py \
	    -- $(basename $(FIGURE_PREVIEW))-$$m.png || exit 1; \
	done

# Configure (if needed) and build only Lavoir.
lavoir:
	@[ -f build/build.ninja ] || cmake -B build -G Ninja
	cmake --build build --target lavoir

# Configure an optimized gameplay build and build only the macOS game.
moppe:
	cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build --target moppe

# Build and run the separately requested test program.
test:
	@[ -f build/build.ninja ] || cmake -B build -G Ninja
	cmake --build build --target moppe-tests
	ctest --test-dir build --output-on-failure

# Build and open the standalone Metal graphics workshop.
atelier:
	@[ -f build/build.ninja ] || cmake -B build -G Ninja
	cmake --build build --target atelier
	open build/atelier.app

# Build and run the Zig quantity-specification workshop.
etalon:
	cd etalon && zig build run

# Run the Zig quantity-specification workshop's tests.
etalon-test:
	cd etalon && zig build test

# Keep the nightly compiler alive and incrementally re-run Étalon's tests.
etalon-watch:
	cd etalon && zig build -fincremental --watch unit

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
	open build-xcode/moppe.xcproj

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

# Bake the default world here, then build and deploy the Xbox game with it.
xbox:
	./tools/deploy-xbox $(XBOX_DEPLOY_ARGS)

# Build the canonical terrain with the Fast profile, capture the Lab, and exit.
# Build a deterministic grove from surface habitat and capture it in-game.
# Compose a frozen, varied landscape survey and its inspectable HTML report.
gazetteer:
	./tools/capture-terrain-gazetteer \
		"$(or $(GAZETTEER_OUT),/tmp/moppe-gazetteer)"

# The grass laboratory: a rolling plain with cover saturated everywhere the
# medium can root and no trees, captured through the gazetteer program. The
# grass-gradient frames then show the pure LOD gradient of the grass system.
# The smoke profile keeps geological time short: a plain needs no epochs.
grass-lab:
	MOPPE_GRASS_LAB=1 MOPPE_UPLIFT_YEARS=0 MOPPE_TERRAIN_PROFILE=smoke \
		./tools/capture-terrain-gazetteer \
		"$(or $(GRASS_LAB_OUT),/tmp/moppe-grass-lab)"

# Capture the curated hydrology gallery and write its HTML/CSV report.
water-benchmark:
	./tools/water-benchmark

# Archive and upload a new build to App Store Connect for TestFlight.
testflight:
	./tools/testflight upload
