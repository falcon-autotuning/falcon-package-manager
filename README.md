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

## Package Management & Dependencies

### Declaring Dependencies

Packages declare their dependencies in ==falcon.yml==:

```YAML
name: my-project
version: 1.0.0
maintainer: Your Name
github: your-org/my-project
license: MPL-2.0

ffi:
  my-wrapper.so: sha256:abc123def456...

dependencies:
  - name: testing
    version: "^1.0.0"
    github: falcon-autotuning/testing
  
  - name: utils
    version: "~1.2.0"
    github: falcon-autotuning/utils
  
  - name: core
    version: "1.3.2"
    github: falcon-autotuning/core
  
  - name: local-lib
    local_path: ../shared/lib
```

### Version Resolution

The package manager resolves dependencies to specific GitHub releases using **SemVer constraints**:

| Constraint | Meaning | Example |
|------------|---------|---------|
| `^1.2.3`   | Caret: Compatible with 1.2.3 (allows changes that don't modify the major version) | Matches 1.2.3, 1.2.4, 1.3.0, but NOT 2.0.0 |
| `~1.2.3`   | Tilde: Compatible with 1.2.x (allows patch-level changes only) | Matches 1.2.3, 1.2.4, but NOT 1.3.0 |
| `1.2.3`    | Exact: Requires exactly this version | Matches only 1.2.3 |
| `*` or empty | Any: Uses the latest release | Matches the most recent release |

### How Version Resolution Works

1. Dependency Declaration: A ==falcon.yml== lists dependencies with SemVer constraints
2. Release Discovery: When resolving, the package manager queries GitHub's releases API
3. Constraint Matching: The highest version matching the constraint is selected
4. Download & Cache: The matched release is downloaded (tarball) and cached locally
5. Validation: FFI binary hashes (from ==falcon.yml==) are verified for security

### Example: Using Versioned Packages

Given ==falcon-autotuning/testing== could have releases: ==v1.0.0==, ==v1.0.1==, ==v1.1.0==, ==v1.2.0==, ==v2.0.0==

```YAML
dependencies:

# Resolves to v1.2.0 (highest matching 1.x)

* name: testing
    version: "^1.0.0"
    github: falcon-autotuning/testing
  
# Resolves to v1.2.0 (highest matching 1.2.x)

* name: testing
    version: "~1.2.0"
    github: falcon-autotuning/testing
  
# Resolves to v2.0.0 (latest)

* name: testing
    github: falcon-autotuning/testing
```

### Release Structure

For version resolution to work, your GitHub releases must include a tarball asset named =={repo}.tar.gz==:

```Code
<https://github.com/owner/repo/releases/download/v1.0.0/repo.tar.gz>
```

The tarball must contain:

- ==falcon.yml== (package manifest with FFI hash mappings)
- ==.fal== files (Falcon DSL scripts)
- ==.so== files (pre-built FFI binaries, matching hashes in manifest)

### Import Statements in .fal Files

```falcon
# Simple import (uses dependency from falcon.yml)

import "testing"

# Explicit package path

import "github.com/falcon-autotuning/testing"

# Specific version (overrides falcon.yml)

import "github.com/falcon-autotuning/testing@v1.0.0"

# Subdirectory or file

import "github.com/falcon-autotuning/utils/math.fal"
```

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
