# MHGU Recomp build
# Windows: MSYS2 UCRT64 (clang, lld, make; see README.md). Linux: clang. macOS: Xcode's clang + Homebrew libraries.
#
#   make gen GAME=<folder with exefs/ and romfs/>   recompile the game code from your own dump into generated/
#   make -j4                                        build build/mhgurecomp(.exe) and the game code library, with its
#                                                   DLLs and the game files (build/data): start build/mhgurecomp.exe
# build/ holds only the game folder (what a player copies); the compiler's files go to obj/.
UNAME := $(shell uname -s)
ifneq ($(filter MINGW% MSYS% CYGWIN% Windows_NT,$(UNAME) $(OS)),)
PLATFORM := windows
else ifeq ($(UNAME),Darwin)
PLATFORM := darwin
else
PLATFORM := linux
endif

GAME     ?= ../mhgu/game
GEN      ?= generated
B        ?= build
O        ?= obj
CC       := clang
CXX      := clang++
ifeq ($(PLATFORM)-$(shell uname -m),darwin-arm64)
GENARCH  ?= -mcpu=apple-m1    # Apple Silicon
else
GENARCH  ?= -march=x86-64-v3  # AVX2 + FMA (Haswell or newer)
endif
GENFLAGS := -O1 -fno-strict-aliasing -ffp-contract=off -fno-math-errno -Wno-return-type -Wno-unused-label \
            -Wno-parentheses-equality -Wno-unused-value -fvisibility=hidden -Iruntime $(GENARCH)

GEN_SRC  := $(wildcard $(GEN)/main/*.c) $(wildcard $(GEN)/sdk/*.c)
GEN_OBJ  := $(patsubst $(GEN)/%.c,$(O)/gen/%.o,$(GEN_SRC)) $(O)/gen/tools/gamelib_entry.o
RT_SRC   := $(wildcard runtime/*.c) $(wildcard runtime/hle/*.c) $(wildcard runtime/services/*.c) $(wildcard runtime/net/*.c) \
            runtime/nvn/objects.c runtime/nvn/cmdbuf.c runtime/nvn/nvn_args.c runtime/nvn/tegra.c runtime/nvn/formats.c \
            runtime/nvn/backend_null.c runtime/nvn/astc.c runtime/nvn/maxwell/translate.c $(wildcard runtime/nvn/vulkan/*.c) \
            runtime/platform/$(PLATFORM).c
# (simd_int.c / neon.c, helpers of the recompiled code, live in the executable: the game code library imports them,
# and runtime/interp.c uses them too)
RT_HDRS  := $(wildcard runtime/*.h) $(wildcard runtime/platform/*.h) $(wildcard runtime/nvn/*.h) $(wildcard runtime/nvn/*/*.h)

ifeq ($(PLATFORM),windows)
EXE      := $(B)/mhgurecomp.exe
OSFLAGS  := -D_FILE_OFFSET_BITS=64 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_RAND_S -D_POSIX_THREAD_SAFE_FUNCTIONS
GENFLAGS += -D_FILE_OFFSET_BITS=64
LDFLAGS  := -fuse-ld=lld -Wl,--stack,16777216 -lSDL3 -lavformat -lavcodec -lavutil -lvulkan-1 -lshaderc_shared -lwinmm -lole32 -lshell32 -luuid -lws2_32 \
            -Wl,--export-all-symbols -Wl,--out-implib,$(O)/mhgurecomp.dll.a
GAMELIB  := $(B)/mhgugame.dll
GAMELIB_LDFLAGS := -shared -fuse-ld=lld $(O)/mhgurecomp.dll.a
else ifeq ($(PLATFORM),darwin)
# Homebrew: SDL3 ffmpeg shaderc vulkan-loader vulkan-headers molten-vk (README.md); VMA in third_party/vk_mem_alloc
BREW     ?= $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
# RELEASE=1 (make release): built for macOS $(MACOS_MIN) and newer, against the libraries tools/macos_deps.sh built
# for it in $(DEPS) (Homebrew's load only on the macOS they were installed on); MoltenVK and the Vulkan headers are
# still Homebrew's
DEPS     ?= third_party/macos-deps
MACOS_MIN ?= 13.0
EXE      := $(B)/mhgurecomp
ifeq ($(RELEASE),1)
MINFLAG  := -mmacosx-version-min=$(MACOS_MIN)
OSFLAGS  := -I$(DEPS)/include -I$(BREW)/include $(MINFLAG)
GENFLAGS += -fPIC $(MINFLAG)
LIBDIRS  := -L$(DEPS)/lib -Wl,-rpath,$(abspath $(DEPS)/lib) $(MINFLAG)  # (the rpath: build/ runs too; make_app drops it)
else
OSFLAGS  := -I$(BREW)/include
GENFLAGS += -fPIC
LIBDIRS  := -L$(BREW)/lib
endif
LDFLAGS  := $(LIBDIRS) -rdynamic -lSDL3 -lavformat -lavcodec -lavutil -lvulkan -lshaderc_shared -lpthread -lm \
            -framework Cocoa
RT_OBJC  := runtime/platform/darwin_menu.m runtime/platform/darwin_settings.m  # (the Settings menu and window)
GAMELIB  := $(B)/mhgugame.dylib
# (a Mach-O bundle: the game code's imports of runtime functions resolve against the executable at link time)
GAMELIB_LDFLAGS := -bundle -bundle_loader $(EXE) $(MINFLAG)
else
EXE      := $(B)/mhgurecomp
OSFLAGS  :=
GENFLAGS += -fPIC
LDFLAGS  := -fuse-ld=lld -rdynamic -lSDL3 -lavformat -lavcodec -lavutil -lvulkan -lshaderc_shared -lpthread -ldl -lm
GAMELIB  := $(B)/mhgugame.so
GAMELIB_LDFLAGS := -shared -fuse-ld=lld
endif

# GAMELIB_ID: what the game code library is compiled against. A library with another id is refused at startup.
GAMELIB_SRCS := tools/gen.py tools/lifter.py tools/neon.py tools/nso.py tools/gamelib_entry.c \
                runtime/hle/hle_symbols.txt runtime/cpu.h runtime/vfp.h runtime/neon_rt.h runtime/gamelib.h
GAMELIB_ID := $(shell (cat $(GAMELIB_SRCS); echo '$(GENFLAGS)') | sha256sum | cut -c1-16)

RT_OBJ   = $(patsubst runtime/%.c,$(O)/rt/%.o,$(RT_SRC)) $(patsubst runtime/%.m,$(O)/rt/%.o,$(RT_OBJC)) $(RT_CXX) \
           $(O)/rt/overlay.o $(IMGUI_OBJ)
# Dear ImGui with cimgui's C API (third_party/cimgui: Forge plugins' ImGui, runtime/forge_imgui.c, overlay.cpp)
IMGUI    := third_party/cimgui
IMGUI_SRC := $(IMGUI)/cimgui.cpp $(addprefix $(IMGUI)/imgui/,imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp)
IMGUI_SRC += $(if $(filter 1,$(NULL_GPU)),,$(IMGUI)/imgui/backends/imgui_impl_vulkan.cpp)
IMGUI_OBJ := $(patsubst $(IMGUI)/%.cpp,$(O)/imgui/%.o,$(IMGUI_SRC))
IMGUIFLAGS := -std=gnu++17 -O2 -g -DCIMGUI_NO_EXPORT -I$(IMGUI) -I$(IMGUI)/imgui
# (vendored VulkanMemoryAllocator fallback: -I on a missing dir is ignored, so MSYS2 builds
# with the system vulkan-memory-allocator package are unaffected)
RTFLAGS  := -O2 -g -fno-strict-aliasing -Wall -Wno-unused-function -Iruntime -Ithird_party/vk_mem_alloc -I$(IMGUI) $(OSFLAGS)
# NULL_GPU=1: no renderer (runtime/nvn/backend_null.c: the game runs, nothing is drawn)
ifeq ($(NULL_GPU),1)
RTFLAGS  += -DMHGU_NULL_BACKEND
RT_SRC   := $(filter-out runtime/nvn/vulkan/%,$(RT_SRC))
RT_CXX   :=
else
RT_CXX   := $(O)/rt/nvn/vulkan/vma.o
endif

all: $(EXE) $(if $(GEN_SRC),$(GAMELIB)) $(if $(filter windows,$(PLATFORM)),bundle) $(if $(filter 1,$(RELEASE)),,data)

$(O)/gen/%.o: $(GEN)/%.c runtime/cpu.h runtime/vfp.h runtime/neon_rt.h
	@mkdir -p $(dir $@)
	@$(CC) $(GENFLAGS) -c $< -o $@


# the id as a file, rewritten only when it changes: both sides of the library interface rebuild together
$(O)/gamelib.id: FORCE
	@mkdir -p $(O); [ "$$(cat $@ 2>/dev/null)" = '$(GAMELIB_ID)' ] || echo '$(GAMELIB_ID)' > $@

$(O)/gen/tools/gamelib_entry.o: tools/gamelib_entry.c $(O)/gamelib.id
	@mkdir -p $(dir $@)
	$(CC) $(GENFLAGS) -DGAMELIB_ID='"$(GAMELIB_ID)"' -c $< -o $@
$(O)/rt/gamelib.o: $(O)/gamelib.id
$(O)/rt/gamelib.o: RTFLAGS += -DGAMELIB_ID='"$(GAMELIB_ID)"'

$(O)/rt/%.o: runtime/%.c $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(RTFLAGS) -c $< -o $@

$(O)/rt/%.o: runtime/%.m $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(RTFLAGS) -fobjc-arc -c $< -o $@

$(O)/rt/forge_imgui.o: runtime/forge_imgui_thunks.inc $(IMGUI)/cimgui.h
$(O)/rt/forge_imgui.o: RTFLAGS += -DCIMGUI_NO_EXPORT -Wno-unused-variable

$(O)/rt/overlay.o: runtime/overlay.cpp runtime/overlay.h runtime/forge.h
	@mkdir -p $(dir $@)
	$(CXX) $(IMGUIFLAGS) $(if $(filter 1,$(NULL_GPU)),-DMHGU_NULL_BACKEND) -Wall -Iruntime $(OSFLAGS) -c $< -o $@

$(O)/imgui/%.o: $(IMGUI)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(IMGUIFLAGS) $(OSFLAGS) -w -c $< -o $@

$(O)/rt/nvn/vulkan/vma.o: runtime/nvn/vulkan/vma.cpp
	@mkdir -p $(dir $@)
	$(CXX) -O2 -w -Ithird_party/vk_mem_alloc $(OSFLAGS) -c $< -o $@

$(EXE): $(RT_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(GAMELIB): $(GEN_OBJ) | $(EXE)
	@mkdir -p $(O)
	$(CC) $(GAMELIB_LDFLAGS) -o $(O)/gamelib.tmp $^
	mv -f $(O)/gamelib.tmp $@

PY := $(firstword $(wildcard tools/venv/Scripts/python.exe) $(wildcard tools/venv/bin/python) python3)

# The build folder is the game folder: mhgurecomp.exe starts from it on its own (runtime/main.c), with
# - bundle (Windows): the MSYS2 DLLs it needs beside it (tools/bundle_dlls.py; not the GPU driver's vulkan-1.dll)
# - data: the game files from $(GAME) in $(B)/data/exefs and $(B)/data/romfs, once (hard links where the file system
#   allows them - no second copy on disk, and deleting them leaves $(GAME) alone - else copies).
# Nothing else: a player copies it as a whole into their game folder (or over it: their settings, caches, mods/ and
# log.txt are written only when the game runs). run-game.bat runs it with those in run/ (MHGU_HOME), not here.
bundle: $(EXE) $(if $(GEN_SRC),$(GAMELIB))
	@$(PY) tools/bundle_dlls.py "$$(cygpath -m "$$(dirname "$$(command -v $(CC))")")" $(B) $^

# (macOS: APFS clones, as free as hard links, which BSD cp can't make for a folder)
CPLINK := $(if $(filter darwin,$(PLATFORM)),cp -cR,cp -rl)
data: $(B)/data/.complete
$(B)/data/.complete:
	@if [ ! -f "$(GAME)/exefs/main" ] || [ ! -d "$(GAME)/romfs" ]; then \
	    echo "data: no game files in $(GAME) (make GAME=<folder with exefs/ and romfs/>): $(B)/data left empty"; \
	else \
	    mkdir -p $(B)/data && for d in exefs romfs; do \
	        rm -rf "$(B)/data/$$d.tmp"; \
	        $(CPLINK) "$(GAME)/$$d" "$(B)/data/$$d.tmp" 2>/dev/null || { rm -rf "$(B)/data/$$d.tmp"; \
	            echo "data: no hard links here, copying $$d"; cp -r "$(GAME)/$$d" "$(B)/data/$$d.tmp"; } || exit 1; \
	        rm -rf "$(B)/data/$$d" && mv "$(B)/data/$$d.tmp" "$(B)/data/$$d" || exit 1; \
	    done && touch $@ && echo "data: game files in $(B)/data"; \
	fi

gen:
	cd tools && ../$(PY) gen.py sdk ../$(GAME)/exefs/sdk 0x02400000 ../$(GEN)/sdk --hle ../runtime/hle/hle_symbols.txt
	cd tools && ../$(PY) gen.py main ../$(GAME)/exefs/main 0x00200000 ../$(GEN)/main

# macOS: a self-contained app of the build (tools/make_app.py): runtime, game code, game files (APFS clones), the
# Homebrew libraries and MoltenVK; it writes to ~/Library/Application Support/MHGURecomp. For you only: never share it.
APP ?= app/MHGURecomp.app
app: all
	python3 tools/make_app.py $(B) $(GAME) $(APP)

# macOS release (the zip players download, as upstream's Windows-/ArchLinux-MHGURecomp.<version>.zip):
#   make macos-deps                        once: the libraries, built for macOS $(MACOS_MIN) (tools/macos_deps.sh)
#   make release VERSION=0.9.11 [BONUS=<folder with the donor save's system file>]
# builds into build-release/ and obj-release/ (your own build/ stays as it is), and makes release/MHGURecomp.app with
# no game files in it (players put theirs in ~/Library/Application Support/MHGURecomp/data), zipped as
# release/macOS-MHGURecomp.$(VERSION).zip.
VERSION ?= $(shell git describe --tags --always 2>/dev/null)
REL_B   ?= build-release
REL_O   ?= obj-release
REL_APP := release/MHGURecomp.app
REL_ZIP := release/macOS-MHGURecomp.$(VERSION).zip
macos-deps:
	sh tools/macos_deps.sh $(DEPS) $(MACOS_MIN)
release:
	@for l in sdl3 vulkan-loader shaderc ffmpeg; do [ "$$(cat $(DEPS)/.$$l 2>/dev/null)" = "$(MACOS_MIN)" ] || \
	    { echo "release: $$l for macOS $(MACOS_MIN) is not in $(DEPS): make macos-deps first"; exit 1; }; done
	@[ -n "$(GEN_SRC)" ] || { echo "release: no generated code in $(GEN): make gen GAME=... first"; exit 1; }
	$(MAKE) RELEASE=1 B=$(REL_B) O=$(REL_O) all
	python3 tools/make_app.py --release --version $(VERSION) --min-macos $(MACOS_MIN) --libs $(DEPS)/lib \
	    $(if $(BONUS),--bonus $(BONUS)) $(REL_B) $(REL_APP)
	rm -f $(REL_ZIP) && ditto -c -k --keepParent $(REL_APP) $(REL_ZIP)
	@echo "release: $(REL_ZIP)"

# differential test of the recompiler against Unicorn (tools/test_lift.py)
test-lift:
	cd tools && ../$(PY) test_lift.py ../$(GAME)/exefs/main 0x00200000 --tests 6000
	cd tools && ../$(PY) test_lift.py ../$(GAME)/exefs/sdk 0x02400000 --tests 6000

runtime-objs: $(RT_OBJ)

clean:  # (keeps $(B)/data, and the settings, caches and saves wherever the game ran)
	rm -rf $(O) $(B)/*.dll $(B)/*.so $(B)/*.dylib $(EXE) $(B)/vkprobe.exe

.PHONY: all app macos-deps release gen test-lift runtime-objs clean bundle data FORCE
