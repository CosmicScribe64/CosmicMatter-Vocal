RACK_DIR ?= ../../.rack-sdk/Rack-SDK

SOURCES += src/E2EDriver.cpp
DISTRIBUTABLES += res
DISTRIBUTABLES += LICENSE

EXTRA_CXXFLAGS += -std=c++17
ifeq ($(shell uname -s),Darwin)
EXTRA_FLAGS += -mmacosx-version-min=10.15
endif

include $(RACK_DIR)/plugin.mk
