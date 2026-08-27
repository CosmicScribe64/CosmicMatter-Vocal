CXX ?= c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -pthread -Isrc -Ithird_party
LDFLAGS := -pthread
ifeq ($(shell uname -s),Darwin)
LDFLAGS += -liconv
endif

CORE := \
	src/core/VocalScore.cpp src/core/Serialization.cpp src/core/ProjectFile.cpp src/core/Encoding.cpp \
	src/voicebank/Voicebank.cpp src/phonemizer/Phonemizer.cpp \
	src/render/Wav.cpp src/render/NativeV1Renderer.cpp src/render/RenderService.cpp \
	src/transport/VocalTransport.cpp src/import/UstxImporter.cpp src/export/UstxExporter.cpp src/dsp/RealtimeVoiceModulation.cpp

BUILD ?= build-tests
RACK_DIR ?= .rack-sdk/Rack-SDK
RACK_RUNTIME_DIR ?= $(RACK_DIR)
RACK_CXXFLAGS := $(CXXFLAGS) -isystem $(RACK_DIR)/include -isystem $(RACK_DIR)/dep/include
RACK_LDFLAGS := $(LDFLAGS) -L$(RACK_DIR) -lRack
ifeq ($(OS),Windows_NT)
RACK_RUN_ENV := PATH=$(RACK_RUNTIME_DIR):$$PATH
else ifeq ($(shell uname -s),Darwin)
RACK_CXXFLAGS += -mmacosx-version-min=10.15
RACK_RUN_ENV := DYLD_LIBRARY_PATH=$(RACK_DIR)
else
RACK_RUN_ENV := LD_LIBRARY_PATH=$(RACK_DIR)
endif

.PHONY: tests offline rack-harness stress clean

$(BUILD):
	mkdir -p $@

$(BUILD)/vocalrack-tests: tests/test_main.cpp $(CORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/vocalrack-render: tools/vocalrack-render.cpp $(CORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/rack-harness: tests/rack_harness.cpp src/rack/VocalModule.cpp $(CORE) | $(BUILD)
	$(CXX) $(RACK_CXXFLAGS) -o $@ $^ $(RACK_LDFLAGS)

tests: $(BUILD)/vocalrack-tests
	$(BUILD)/vocalrack-tests

offline: $(BUILD)/vocalrack-tests $(BUILD)/vocalrack-render
	$(BUILD)/vocalrack-tests --singer res/singers/adachi-rei --offline
	mkdir -p test-artifacts/offline
	$(BUILD)/vocalrack-render --phonemizer "Japanese Auto" --singer res/singers/adachi-rei --score tests/fixtures/rei_phrase.json --out test-artifacts/offline/offline-rei-phrase.wav --bpm 120
	$(BUILD)/vocalrack-render --phonemizer "Japanese Auto" --singer res/singers/adachi-rei --score tests/fixtures/rei_phrase.json --out test-artifacts/offline/offline-rei-phrase-60.wav --bpm 60
	$(BUILD)/vocalrack-render --phonemizer "Japanese Auto" --singer res/singers/adachi-rei --score tests/fixtures/rei_phrase.json --out test-artifacts/offline/offline-rei-phrase-plus7.wav --bpm 120 --transpose 7
	$(BUILD)/vocalrack-render --phonemizer "Japanese Auto" --singer res/singers/adachi-rei --project tests/fixtures/legacy_song.ust --out test-artifacts/offline/legacy-song-import.wav --sample-rate 44100

rack-harness: $(BUILD)/rack-harness
	$(RACK_RUN_ENV) $(BUILD)/rack-harness

stress: $(BUILD)/rack-harness
	$(RACK_RUN_ENV) $(BUILD)/rack-harness --stress

clean:
	rm -rf $(BUILD)
