.PHONY: all configure build-debug build-release test test-debug test-verbose \
        clean install uninstall clangd-helpers help

# ── OS detection ──────────────────────────────────────────────────────────────
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    PLATFORM        := linux
    CMAKE_GENERATOR := Ninja
    VCPKG_TRIPLET   ?= x64-linux-dynamic
    NPROC           := $(shell nproc 2>/dev/null || echo 4)
    SUDO            ?= sudo
		export CC=clang
		export CXX=clang++
endif
ifeq ($(OS),Windows_NT)
    PLATFORM        := windows
    CMAKE_GENERATOR := "Visual Studio 17 2022"
    VCPKG_TRIPLET   ?= x64-windows
    NPROC           := 4
    SUDO            :=
endif

ENV_FILE := .nuget-credentials
ifeq ($(wildcard $(ENV_FILE)),)
  $(info [Makefile] $(ENV_FILE) not found, skipping environment sourcing)
else
  include $(ENV_FILE)
  export $(shell sed 's/=.*//' $(ENV_FILE) | xargs)
  $(info [Makefile] Loaded environment from $(ENV_FILE))
endif
# ── Paths ─────────────────────────────────────────────────────────────────────
VCPKG_ROOT ?= $(CURDIR)/vcpkg
VCPKG_TOOLCHAIN ?= $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
VCPKG_INSTALLED_DIR ?= $(CURDIR)/vcpkg_installed
FEED_URL ?= 
NUGET_API_KEY ?=
FEED_NAME ?= 
USERNAME ?=
VCPKG_BINARY_SOURCES ?= ""
ifeq ($(strip $(FEED_URL)),)
  CMAKE_VCPKG_BINARY_SOURCES :=
else
	VCPKG_BINARY_SOURCES := "clear;nuget,$(FEED_URL),readwrite"
  CMAKE_VCPKG_BINARY_SOURCES := -DVCPKG_BINARY_SOURCES=$(VCPKG_BINARY_SOURCES)
endif

BUILD_DIR_DEBUG   := build/debug
BUILD_DIR_RELEASE := build/release

INSTALL_PREFIX    ?= /opt/falcon
INSTALL_LIBDIR    := $(INSTALL_PREFIX)/lib
INSTALL_INCLUDEDIR := $(INSTALL_PREFIX)/include
INSTALL_CMAKEDIR  := $(INSTALL_LIBDIR)/cmake/falcon-typing

# ─────────────────────────────────────────────────────────────────────────────
all: build-release

help:
	@echo "Falcon Package Manager Build System"
	@echo "=========================="
	@echo ""
	@echo "Build targets:"
	@echo "  make build-debug    - Build debug version"
	@echo "  make build-release  - Build release version (default)"
	@echo "  make configure      - Configure both builds"
	@echo "  make clean          - Remove all build artifacts"
	@echo ""
	@echo "Test targets:"
	@echo "  make test           - Build release and run all tests"
	@echo "  make test-debug     - Build debug and run all tests"
	@echo "  make test-verbose   - Run tests with verbose CTest output"
	@echo ""
	@echo "Install targets:"
	@echo "  make install        - Install to $(INSTALL_PREFIX)"
	@echo "  make uninstall      - Remove installed files from $(INSTALL_PREFIX)"
	@echo ""
	@echo "Misc:"
	@echo "  make clangd-helpers - Symlink compile_commands.json for clangd"
	@echo ""
	@echo "Current configuration:"
	@echo "  Platform : $(PLATFORM)"
	@echo "  Generator: $(CMAKE_GENERATOR)"
	@echo "  Triplet  : $(VCPKG_TRIPLET)"

# ── vcpkg guard ───────────────────────────────────────────────────────────────
.PHONY: vcpkg-bootstrap
vcpkg-bootstrap:
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
		echo "Cloning vcpkg..."; \
		git clone https://github.com/microsoft/vcpkg.git $(VCPKG_ROOT); \
	fi
	@if [ ! -f "$(VCPKG_ROOT)/vcpkg" ]; then \
		echo "Bootstrapping vcpkg..."; \
		cd $(VCPKG_ROOT) && ./bootstrap-vcpkg.sh; \
	fi

setup-nuget-auth:
	@if [ -z "$$NUGET_API_KEY" ]; then \
		echo "No NUGET_API_KEY found, skipping NuGet setup (local-only build, no binary cache)."; \
		exit 0; \
	fi
	@echo "Setting up NuGet authentication for vcpkg binary caching..."
	@if ! command -v mono >/dev/null 2>&1; then \
		echo "Error: mono is not installed. Please install mono (e.g., 'sudo pacman -S mono' on Arch, 'sudo apt install mono-complete' on Ubuntu)."; \
		exit 1; \
	fi
	@NUGET_EXE=$$(vcpkg fetch nuget | tail -n1); \
	mono "$$NUGET_EXE" sources remove -Name "$(FEED_NAME)" || true; \
	mono "$$NUGET_EXE" sources add -Name "$(FEED_NAME)" -Source "$(FEED_URL)" -Username "$(USERNAME)" -Password "$(NUGET_API_KEY)"

.PHONY: vcpkg-install-deps
vcpkg-install-deps: setup-nuget-auth 
	@echo "Installing vcpkg dependencies" 
	VCPKG_FEATURE_FLAGS=binarycaching \
		$(VCPKG_ROOT)/vcpkg install \
		--binarysource=$(VCPKG_BINARY_SOURCES) \
		--triplet="$(VCPKG_TRIPLET)"

check-vcpkg: vcpkg-bootstrap  vcpkg-install-deps
	@echo "Checking vcpkg configuration..."
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
		echo "Error: vcpkg not found at $(VCPKG_ROOT)"; \
		echo "Run 'make deps' in the parent directory first"; \
		exit 1; \
	fi
	@if [ ! -f "$(VCPKG_TOOLCHAIN)" ]; then \
		echo "Error: vcpkg toolchain not found at $(VCPKG_TOOLCHAIN)"; \
		exit 1; \
	fi
	@echo "✓ vcpkg configuration OK"

# ── Configure ─────────────────────────────────────────────────────────────────
configure-debug: check-vcpkg
	@echo "Configuring debug build..."
	@mkdir -p $(BUILD_DIR_DEBUG)
	cd $(BUILD_DIR_DEBUG) && cmake ../.. \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
		-DVCPKG_INSTALLED_DIR=$(VCPKG_INSTALLED_DIR) \
		-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) \
		-DBUILD_TESTS=ON \
		-DUSE_CCACHE=ON \
		-DENABLE_PCH=ON \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		$(CMAKE_VCPKG_BINARY_SOURCES) \
		-G $(CMAKE_GENERATOR)
	@echo "✓ Debug build configured"

configure-release: check-vcpkg
	@echo "Configuring release build..."
	@mkdir -p $(BUILD_DIR_RELEASE)
	cd $(BUILD_DIR_RELEASE) && cmake ../.. \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
		-DVCPKG_INSTALLED_DIR=$(VCPKG_INSTALLED_DIR) \
		-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) \
		-DBUILD_TESTS=ON \
		-DUSE_CCACHE=ON \
		-DENABLE_PCH=ON \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		$(CMAKE_VCPKG_BINARY_SOURCES) \
		-G $(CMAKE_GENERATOR)
	@echo "✓ Release build configured"

configure: configure-debug configure-release

# ── Build ─────────────────────────────────────────────────────────────────────
build-debug: configure-debug
	@echo "Building debug..."
	ninja -C $(BUILD_DIR_DEBUG) -j$(NPROC)
	@echo "✓ Debug build complete"
	@$(MAKE) clangd-helpers

build-release: configure-release
	@echo "Building release..."
	ninja -C $(BUILD_DIR_RELEASE) -j$(NPROC)
	@echo "✓ Release build complete"

# ── Test ──────────────────────────────────────────────────────────────────────
test: build-release
	@echo "Running tests (release)..."
	@cd $(BUILD_DIR_RELEASE) && \
		LD_LIBRARY_PATH=$(CURDIR)/$(BUILD_DIR_RELEASE):$(VCPKG_INSTALLED_DIR)/$(VCPKG_TRIPLET)/lib:$(INSTALL_PREFIX)/lib:$$LD_LIBRARY_PATH \
		ctest -C Release --output-on-failure
	@echo "✓ All tests passed"

test-debug: build-debug
	@echo "Running tests (debug)..."
	@cd $(BUILD_DIR_DEBUG) && \
		LD_LIBRARY_PATH=$(CURDIR)/$(BUILD_DIR_DEBUG):$(VCPKG_INSTALLED_DIR)/$(VCPKG_TRIPLET)/lib:$(INSTALL_PREFIX)/lib:$$LD_LIBRARY_PATH \
		ctest -C Debug --output-on-failure
	@echo "✓ All debug tests passed"

test-verbose: build-release
	@echo "Running tests (verbose)..."
	@cd $(BUILD_DIR_RELEASE) && \
		LD_LIBRARY_PATH=$(CURDIR)/$(BUILD_DIR_RELEASE):$(VCPKG_INSTALLED_DIR)/$(VCPKG_TRIPLET)/lib:$(INSTALL_PREFIX)/lib:$$LD_LIBRARY_PATH \
		ctest -C Release --verbose
	@echo "✓ Done"

# ── Install ───────────────────────────────────────────────────────────────────
install: build-release
	@echo "Installing falcon-typing to $(INSTALL_PREFIX)..."
	$(SUDO) cmake --install $(BUILD_DIR_RELEASE) --prefix $(INSTALL_PREFIX)
	@echo "✓ Installation complete"
	@echo "  Library : $(INSTALL_LIBDIR)/libfalcon-typing.so"
	@echo "  Headers : $(INSTALL_INCLUDEDIR)/falcon-typing/"
	@echo "  CMake   : $(INSTALL_CMAKEDIR)/"

# ── Uninstall ─────────────────────────────────────────────────────────────────
uninstall:
	@echo "Uninstalling falcon-typing from $(INSTALL_PREFIX)..."
	$(SUDO) rm -f  $(INSTALL_LIBDIR)/libfalcon-typing.so
	$(SUDO) rm -f  $(INSTALL_LIBDIR)/libfalcon-typing.so.1
	$(SUDO) rm -f  $(INSTALL_LIBDIR)/libfalcon-typing.so.1.0.0
	$(SUDO) rm -r $(INSTALL_INCLUDEDIR)/falcon-typing
	$(SUDO) rm -r $(INSTALL_CMAKEDIR)
	@echo "✓ Uninstall complete"

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR_DEBUG) $(BUILD_DIR_RELEASE) build/ compile_commands.json ./vcpkg_installed/
	@echo "✓ Clean complete"

# ── clangd helpers ────────────────────────────────────────────────────────────
clangd-helpers:
	@if [ -f $(BUILD_DIR_DEBUG)/compile_commands.json ]; then \
		ln -sf $(BUILD_DIR_DEBUG)/compile_commands.json compile_commands.json; \
		echo "✓ clangd compile_commands.json symlinked"; \
	else \
		echo "No compile_commands.json found in debug build directory."; \
	fi
