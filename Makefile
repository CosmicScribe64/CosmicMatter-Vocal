RACK_DIR ?= .rack-sdk/Rack-SDK

FLAGS += -Isrc -Ithird_party
EXTRA_CXXFLAGS += -std=c++17
ifeq ($(shell uname -s),Darwin)
EXTRA_FLAGS += -mmacosx-version-min=10.15
endif
ifeq ($(OS),Windows_NT)
EXTRA_LDFLAGS += -liconv
endif

CORE_SOURCES := \
	src/core/VocalScore.cpp \
	src/core/Serialization.cpp \
	src/core/ProjectFile.cpp \
	src/core/Encoding.cpp \
	src/voicebank/Voicebank.cpp \
	src/phonemizer/Phonemizer.cpp \
	src/render/Wav.cpp \
	src/render/NativeV1Renderer.cpp \
	src/render/RenderService.cpp \
	src/transport/VocalTransport.cpp \
	src/import/UstxImporter.cpp \
	src/export/UstxExporter.cpp \
	src/dsp/RealtimeVoiceModulation.cpp

SOURCES += $(CORE_SOURCES)
SOURCES += src/plugin.cpp
SOURCES += src/rack/VocalModule.cpp
SOURCES += src/rack/VocalWidget.cpp
SOURCES += src/rack/VocalEditor.cpp
SOURCES += src/rack/SingerPlateModule.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += LICENSE
DISTRIBUTABLES += README.md
DISTRIBUTABLES += MANUAL.md
DISTRIBUTABLES += ARCHITECTURE.md
DISTRIBUTABLES += THIRD_PARTY_NOTICES.md
DISTRIBUTABLES += CHANGELOG.md
DISTRIBUTABLES += CONTRIBUTING.md
DISTRIBUTABLES += test-artifacts/e2e/TEST_REPORT.md
DISTRIBUTABLES += test-artifacts/openutau-regression/TEST_REPORT.md
DISTRIBUTABLES += test-artifacts/openutau-english-regression/TEST_REPORT.md
DISTRIBUTABLES += test-artifacts/import-corpus/TEST_REPORT.md
DISTRIBUTABLES += test-artifacts/roundtrip/TEST_REPORT.md
DISTRIBUTABLES += third_party/adachi_rei
DISTRIBUTABLES += third_party/nlohmann/LICENSE.MIT
DISTRIBUTABLES += third_party/lucide/LICENSE
DISTRIBUTABLES += patches/VocalRack-Demo.vcv

all: validate-assets
dist: validate-assets

.PHONY: core-tests offline-tests rack-harness stress test docker-test openutau-compare openutau-regression openutau-english-regression openutau-roundtrip import-corpus validate-assets e2e release

core-tests:
	$(MAKE) -f tests/Makefile tests

offline-tests:
	$(MAKE) -f tests/Makefile offline

rack-harness:
	$(MAKE) -f tests/Makefile rack-harness

stress:
	$(MAKE) -f tests/Makefile stress

test: core-tests offline-tests rack-harness stress

docker-test:
	sh scripts/docker_build_and_test.sh

openutau-compare:
	sh scripts/compare_openutau.sh

openutau-regression:
	sh scripts/run_openutau_regression.sh

openutau-english-regression:
	sh scripts/run_openutau_english_regression.sh

openutau-roundtrip:
	sh scripts/run_openutau_roundtrip.sh

import-corpus:
	test -n "$(CORPUS)" || { echo "usage: make import-corpus CORPUS=/path/to/file-or-folder" >&2; exit 2; }
	$(MAKE) -f tests/Makefile build-tests/vocalrack-render
	python3 scripts/test_import_corpus.py --output test-artifacts/import-corpus/results.json "$(CORPUS)"

validate-assets:
	sh scripts/validate_release.sh

# Build the Rack archive and record an external checksum without attempting to
# embed the archive's hash inside the archive itself.
release: dist
	sh scripts/write_release_checksums.sh dist

e2e: all
	sh scripts/run_rack_e2e.sh

include $(RACK_DIR)/plugin.mk
