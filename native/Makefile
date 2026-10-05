# OpenNOW native client — dependency-free build.
#
#   make                 # auto-detect a platform backend
#   make PLATFORM=win32  # Win32 + OpenGL 3.2 (default on Windows)
#   make PLATFORM=glfw   # GLFW + OpenGL 3 (Linux/macOS, needs GLFW)
#   make PLATFORM=headless   # no window; for tests and CI
#   make test            # headless build + unit checks + i18n key audit
#   make check-keys      # audit the tr() keys against locales/en.json
#   make clean
#
# CMake users: cmake -S . -B build && cmake --build build

CXX      ?= g++
CC       ?= gcc
AR       ?= ar

# ------------------------------------------------------------------ platform --
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows_NT)
ifeq ($(PLATFORM),)
  ifeq ($(UNAME_S),Windows_NT)
    PLATFORM := win32
  else
    # Prefer a real windowed build when GLFW is available; fall back to the
    # headless backend (tests, CI) when it is not.
    GLFW_PROBE := $(shell printf '#include <GLFW/glfw3.h>\nint main(){return 0;}\n' > /tmp/onow_glfw_probe.c 2>/dev/null && $(CC) /tmp/onow_glfw_probe.c -lglfw -o /tmp/onow_glfw_probe >/dev/null 2>&1 && echo yes)
    ifeq ($(GLFW_PROBE),yes)
      PLATFORM := glfw
    else
      PLATFORM := headless
    endif
  endif
endif

DEFINES := -DONOW_PLATFORM_$(shell echo $(PLATFORM) | tr a-z A-Z)

# --------------------------------------------------------------------- flags --
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
INCLUDES := -Iinclude -Ithird_party/imgui -Ithird_party/imgui/backends
LDFLAGS  ?=

IMGUI_BACKENDS :=

ifeq ($(PLATFORM),win32)
  DEFINES += -DONOW_TLS_SCHANNEL -DONOW_AUDIO_WASAPI -DWIN32_LEAN_AND_MEAN
  DEFINES += -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM
  IMGUI_BACKENDS += third_party/imgui/backends/imgui_impl_win32.cpp
  IMGUI_BACKENDS += third_party/imgui/backends/imgui_impl_opengl3.cpp
  LDFLAGS += -lopengl32 -lws2_32 -lsecur32 -lcrypt32 -lwinmm -lgdi32 -luser32 -lshell32 -ladvapi32 -limm32
  LDFLAGS += -mwindows
else ifeq ($(PLATFORM),glfw)
  DEFINES += -DONOW_TLS_OPENSSL -DONOW_AUDIO_NONE -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM
  IMGUI_BACKENDS += third_party/imgui/backends/imgui_impl_glfw.cpp
  IMGUI_BACKENDS += third_party/imgui/backends/imgui_impl_opengl3.cpp
  LDFLAGS += -lpthread -ldl
else
  DEFINES += -DONOW_TLS_OPENSSL -DONOW_AUDIO_NONE
  LDFLAGS += -lpthread -ldl
endif

BUILD    := build/$(PLATFORM)
IMGUI_SRC := third_party/imgui/imgui.cpp \
             third_party/imgui/imgui_draw.cpp \
             third_party/imgui/imgui_tables.cpp \
             third_party/imgui/imgui_widgets.cpp

SRC_DIRS := src src/app src/app/views src/gfx src/model src/net src/services src/stream src/ui src/i18n
APP_SRC  := $(wildcard $(addsuffix /*.cpp,$(SRC_DIRS)))
# Exactly one platform backend per build: each defines create_platform().
ifeq ($(PLATFORM),win32)
  APP_SRC += src/platform/PlatformWin32.cpp
else ifeq ($(PLATFORM),glfw)
  APP_SRC += src/platform/PlatformGlfw.cpp
else
  APP_SRC += src/platform/PlatformHeadless.cpp
endif
IMGUI_SRC := $(filter-out third_party/imgui/imgui_demo.cpp,$(IMGUI_SRC))

OBJ      := $(patsubst %.cpp,$(BUILD)/%.o,$(APP_SRC)) $(patsubst %.cpp,$(BUILD)/%.o,$(IMGUI_SRC)) \
            $(patsubst %.cpp,$(BUILD)/%.o,$(IMGUI_BACKENDS))
DEPS     := $(OBJ:.o=.d)
TARGET   := $(BUILD)/opennow

.PHONY: all clean test check-keys i18n run

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -c $< -o $@

i18n:
	python3 tools/gen_i18n.py

# Every tr("...") key in the sources must exist in locales/en.json.
check-keys:
	python3 tools/check_i18n_keys.py

test: PLATFORM := headless
test: $(TARGET) check-keys
	@echo "== unit checks =="
	@$(TARGET) --self-test
	@echo "== headless smoke run =="
	@$(TARGET) --headless --max-frames 60

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build

-include $(DEPS)
