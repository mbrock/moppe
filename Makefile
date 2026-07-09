.PHONY: archive phone testflight

# Produce a signed App Store archive without uploading it.
archive:
	./tools/testflight archive

# Build, install, and launch Moppe on the paired iPhone.
phone:
	./tools/install-ios

# Archive and upload a new build to App Store Connect for TestFlight.
testflight:
	./tools/testflight upload
