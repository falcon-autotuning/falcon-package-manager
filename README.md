# falcon-package-manager

Package manager for organizing Falcon autotuning scripts and FFI executables.

This module provides a centralized package management system for the Falcon ecosystem,
enabling organization, distribution, and execution of Falcon autotuning scripts alongside
their corresponding FFI (Foreign Function Interface) executables. It streamlines deployment
of measurement routines and ensures proper dependency resolution across the Falcon DSL
components.

---

## Quick Start

### 1. Build & Install

```bash
# From falcon-lib root
make package-manager    # shortcut if defined in the root Makefile, OR:

cd package-manager
make install            # builds release + installs to /opt/falcon
```

### 2. Use in another project

```CMake  
find_package(falcon-package-manager CONFIG REQUIRED
  PATHS /opt/falcon/lib/cmake/falcon-package-manager
)

target_link_libraries(my-target PRIVATE falcon::falcon-package-manager)
```

```Cpp
#include "falcon-package-manager/PackageManager.hpp"

// Create a package manager instance
falcon::pkg::PackageManager pm("/opt/falcon/packages");

// Load a package
auto pkg = pm.load_package("measurement-suite-v1.0");

// Retrieve a script and its FFI executables
auto script = pkg.get_script("calibration.fal");
auto executables = pkg.get_executables();
```

---

## API Overview

### PackageManager.hpp

Main interface for package discovery, loading, and lifecycle management.

| Method                                       | Description                                  |
|-------------------------------------------|-----------------------------------------------------|
| load_package(const std::string &name)      | Load a package by name from the registry            |
| list_available_packages()                 | Enumerate all discoverable packages                 |
| install_package(const std::string &path)  | Install a new package from a local directory        |
| uninstall_package(const std::string &name)| Remove an installed package                         |
| get_package_version(const std::string &name) | Query version of an installed package             |

### ScriptLoader.hpp

Handles loading and validation of Falcon DSL scripts from package bundles.

| Method                                 | Description                                         |
|-----------------------------------------|-----------------------------------------------------|
| load_script(const std::string &path)    | Parse and validate a Falcon DSL script file         |
| validate_syntax()                      | Check script for syntax errors                      |
| extract_dependencies()                  | Identify required FFI executables and libraries     |

### ExecutableRegistry.hpp

Tracks and resolves FFI executables bundled with packages.

| Method                                              | Description                                         |
|-----------------------------------------------------|-----------------------------------------------------|
| register_executable(const std::string &name, const std::string &path) | Register an FFI executable location                |
| resolve_symbol(const std::string &symbol)           | Look up a symbol across registered executables      |
| get_executable(const std::string &name)             | Retrieve executable metadata and path               |
| link_executables()                                 | Verify all executable dependencies are satisfied    |

### PackageMetadata.hpp

Package metadata structure for version and dependency tracking.

```Cpp
struct PackageMetadata {
  std::string name;
  std::string version;
  std::string description;
  std::vector<std::string> dependencies;
  std::map<std::string, std::string> scripts;
  std::vector<std::string> ffi_executables;
};
```

## Building from Source

```bash
cd package-manager
mkdir -p build/release && cd build/release
cmake ../.. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=../../.vcpkg/scripts/buildsystems/vcpkg.cmake \
  -Dfalcon_typing_DIR=/opt/falcon/lib/cmake/falcon_typing \
  -Dfalcon_routine_DIR=/opt/falcon/lib/cmake/falcon_routine \
  -G Ninja
ninja
sudo cmake --install . --prefix /opt/falcon
```

## Running Tests

```bash
cd package-manager
make test              # release
make test-debug        # debug
make test-verbose      # verbose output
```

Tests validate package loading, script parsing, executable resolution, and dependency verification using Google Test.

## Uninstalling

```bash
cd package-manager
make uninstall
```

## License

MPL-2.0
