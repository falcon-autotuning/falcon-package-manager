#include "falcon-package-manager/PackageCache.hpp"
#include "falcon-package-manager/PackageManifest.hpp"
#include "falcon-package-manager/PackageResolver.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

class MultiModulePackageTest : public ::testing::Test {
protected:
  fs::path tmp_;

  void SetUp() override {
    tmp_ = fs::temp_directory_path() / "falcon_pm_multimodule_test";
    fs::create_directories(tmp_);
  }

  void TearDown() override { fs::remove_all(tmp_); }

  /**
   * @brief Create a multi-module package structure
   *
   * Example:
   *   collections/
   *     falcon.yml
   *     array/
   *       array.fal
   *       array-wrapper.so
   *     map/
   *       map.fal
   *       map-wrapper.so
   *     shared/
   *       helpers.fal
   */
  void create_multimodule_package(const fs::path &pkg_dir,
                                  const std::string &name,
                                  const std::string &version,
                                  const std::vector<std::string> &modules) {
    fs::create_directories(pkg_dir);

    // Create main manifest
    std::string manifest = R"(
name: )" + name + R"(
version: )" + version + R"(
maintainer: Test Author
github: owner/)" + name + R"(
license: MPL-2.0
ffi:
)";

    for (const auto &mod : modules) {
      manifest += "  " + mod + "/" + mod + "-wrapper.so: sha256:";
      manifest += std::string(64, 'a') + "\n";
    }

    std::ofstream manifest_file(pkg_dir / "falcon.yml");
    manifest_file << manifest;
    manifest_file.close();

    // Create modules
    for (const auto &mod : modules) {
      auto mod_dir = pkg_dir / mod;
      fs::create_directories(mod_dir);

      // Create module .fal file
      std::ofstream fal(mod_dir / (mod + ".fal"));
      fal << "routine " << mod << "_routine() {}";
      fal.close();

      // Create wrapper (dummy file)
      std::ofstream wrapper(mod_dir / (mod + "-wrapper.so"));
      wrapper << "dummy binary";
      wrapper.close();
    }
  }

  /**
   * @brief Create a simple single-module package
   */
  void create_single_module_package(const fs::path &pkg_dir,
                                    const std::string &name,
                                    const std::string &version) {
    fs::create_directories(pkg_dir);

    std::string manifest = R"(
name: )" + name + R"(
version: )" + version + R"(
maintainer: Test Author
github: owner/)" + name + R"(
license: MPL-2.0
ffi:
  )" + name + R"(-wrapper.so: sha256:)" +
                           std::string(64, 'a') + R"(
dependencies: []
)";

    std::ofstream manifest_file(pkg_dir / "falcon.yml");
    manifest_file << manifest;
    manifest_file.close();

    // Create main .fal file
    std::ofstream fal(pkg_dir / (name + ".fal"));
    fal << "routine " << name << "() {}";
    fal.close();

    // Create wrapper
    std::ofstream wrapper(pkg_dir / (name + "-wrapper.so"));
    wrapper << "dummy binary";
    wrapper.close();
  }
};

// ============================================================================
// Test: Single-Module Package (Baseline)
// ============================================================================

TEST_F(MultiModulePackageTest, SingleModulePackageStructure) {
  create_single_module_package(tmp_ / "simple", "simple", "1.0.0");

  EXPECT_TRUE(fs::exists(tmp_ / "simple" / "falcon.yml"));
  EXPECT_TRUE(fs::exists(tmp_ / "simple" / "simple.fal"));
  EXPECT_TRUE(fs::exists(tmp_ / "simple" / "simple-wrapper.so"));
}

TEST_F(MultiModulePackageTest, SingleModuleIsDetected) {
  create_single_module_package(tmp_ / "simple", "simple", "1.0.0");

  EXPECT_TRUE(falcon::pm::PackageResolver::is_package(tmp_ / "simple"));
}

// ============================================================================
// Test: Multi-Module Package Structure
// ============================================================================

TEST_F(MultiModulePackageTest, MultiModulePackageStructure) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map"});

  EXPECT_TRUE(fs::exists(tmp_ / "collections" / "falcon.yml"));
  EXPECT_TRUE(fs::exists(tmp_ / "collections" / "array" / "array.fal"));
  EXPECT_TRUE(fs::exists(tmp_ / "collections" / "array" / "array-wrapper.so"));
  EXPECT_TRUE(fs::exists(tmp_ / "collections" / "map" / "map.fal"));
  EXPECT_TRUE(fs::exists(tmp_ / "collections" / "map" / "map-wrapper.so"));
}

TEST_F(MultiModulePackageTest, MultiModuleIsDetected) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map"});

  EXPECT_TRUE(falcon::pm::PackageResolver::is_package(tmp_ / "collections"));
}

TEST_F(MultiModulePackageTest, MultiModuleManifestLoading) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map"});

  auto manifest =
      falcon::pm::PackageManifest::load(tmp_ / "collections" / "falcon.yml");

  EXPECT_EQ(manifest.name, "collections");
  EXPECT_EQ(manifest.version, "1.0.0");
  EXPECT_EQ(manifest.ffi.size(), 2u);
  EXPECT_NE(manifest.ffi.find("array/array-wrapper.so"), manifest.ffi.end());
  EXPECT_NE(manifest.ffi.find("map/map-wrapper.so"), manifest.ffi.end());
}

// ============================================================================
// Test: Discovering Packages in Multi-Module Directory
// ============================================================================

TEST_F(MultiModulePackageTest, DiscoverMultiModulePackage) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map"});

  auto discovered = falcon::pm::PackageResolver::discover_packages(tmp_);

  // Should find exactly one package (the collections root)
  ASSERT_EQ(discovered.size(), 1);
  EXPECT_EQ(fs::canonical(discovered[0]), fs::canonical(tmp_ / "collections"));
}

TEST_F(MultiModulePackageTest, DiscoverMultiplePackages) {
  create_single_module_package(tmp_ / "pkg1", "pkg1", "1.0.0");
  create_multimodule_package(tmp_ / "pkg2", "pkg2", "1.0.0", {"mod1", "mod2"});

  auto discovered = falcon::pm::PackageResolver::discover_packages(tmp_);

  ASSERT_EQ(discovered.size(), 2);
}

// ============================================================================
// Test: Module Resolution within Multi-Module Package
// ============================================================================

TEST_F(MultiModulePackageTest, ResolveModuleWithSubpath) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map"});

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  // Import a specific module using subpath
  auto importing = tmp_ / "main.fal";
  auto resolved = resolver.resolve("./collections/array/array.fal", importing);

  EXPECT_EQ(resolved.module_name, "array");
  EXPECT_TRUE(resolved.is_package);
}

TEST_F(MultiModulePackageTest, ResolveModuleWithoutFileExtension) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map"});

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  // Import module by subpath without .fal extension
  // This simulates: import "github.com/owner/collections/array"
  auto importing = tmp_ / "main.fal";
  auto resolved = resolver.resolve("./collections/array", importing);

  EXPECT_EQ(resolved.module_name, "array");
  EXPECT_TRUE(resolved.is_package);
  EXPECT_TRUE(resolved.absolute_path.string().find("array.fal") !=
              std::string::npos);
}

TEST_F(MultiModulePackageTest, ResolveMultipleModulesFromSamePackage) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map", "set"});

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  auto importing = tmp_ / "main.fal";

  std::vector<std::string> imports = {"./collections/array",
                                      "./collections/map", "./collections/set"};
  auto resolved_all = resolver.resolve_all(imports, importing);

  ASSERT_EQ(resolved_all.size(), 3);
  EXPECT_EQ(resolved_all[0].module_name, "array");
  EXPECT_EQ(resolved_all[1].module_name, "map");
  EXPECT_EQ(resolved_all[2].module_name, "set");
}

// ============================================================================
// Test: Local Package Resolution with Modules
// ============================================================================

TEST_F(MultiModulePackageTest, LocalMultiModulePackageResolution) {
  fs::create_directories(tmp_ / "project" / "src");
  create_multimodule_package(tmp_ / "project" / "libs" / "data_structures",
                             "data_structures", "1.0.0",
                             {"vector", "array", "map"});

  auto cache_dir = tmp_ / "project" / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_ / "project", cache);

  auto importing = tmp_ / "project" / "src" / "main.fal";
  auto resolved = resolver.resolve("../libs/data_structures/vector", importing);

  EXPECT_EQ(resolved.module_name, "vector");
  EXPECT_TRUE(resolved.is_package);
}

// ============================================================================
// Test: Error Handling in Multi-Module Packages
// ============================================================================

TEST_F(MultiModulePackageTest, ResolveNonexistentModule) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map"});

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  auto importing = tmp_ / "main.fal";
  EXPECT_THROW(resolver.resolve("./collections/nonexistent", importing),
               std::runtime_error);
}

TEST_F(MultiModulePackageTest, ResolveSingleModuleWithoutExplicitPath) {
  create_single_module_package(tmp_ / "simple", "simple", "1.0.0");

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  auto importing = tmp_ / "main.fal";
  auto resolved = resolver.resolve("./simple", importing);

  EXPECT_EQ(resolved.module_name, "simple");
  EXPECT_TRUE(resolved.is_package);
}

// ============================================================================
// Test: Complex Multi-Module Scenarios
// ============================================================================

TEST_F(MultiModulePackageTest, LargeMultiModulePackage) {
  std::vector<std::string> modules;
  for (int i = 0; i < 10; ++i) {
    modules.push_back("module" + std::to_string(i));
  }
  create_multimodule_package(tmp_ / "large_pkg", "large_pkg", "2.0.0", modules);

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  auto importing = tmp_ / "main.fal";

  // Resolve a few modules
  auto m0 = resolver.resolve("./large_pkg/module0", importing);
  auto m5 = resolver.resolve("./large_pkg/module5", importing);
  auto m9 = resolver.resolve("./large_pkg/module9", importing);

  EXPECT_EQ(m0.module_name, "module0");
  EXPECT_EQ(m5.module_name, "module5");
  EXPECT_EQ(m9.module_name, "module9");
}

TEST_F(MultiModulePackageTest, MultiModuleWithSharedDependencies) {
  auto pkg_dir = tmp_ / "collections";
  fs::create_directories(pkg_dir);

  // Create manifest
  std::string manifest = R"(
name: collections
version: 1.0.0
maintainer: Test Author
github: owner/collections
license: MPL-2.0
ffi:
  array/array-wrapper.so: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
  map/map-wrapper.so: sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
dependencies: []
)";

  std::ofstream manifest_file(pkg_dir / "falcon.yml");
  manifest_file << manifest;
  manifest_file.close();

  // Create shared helper
  auto shared_dir = pkg_dir / "shared";
  fs::create_directories(shared_dir);
  std::ofstream helpers(shared_dir / "helpers.fal");
  helpers << "routine hash(int x) -> (int h) { h = x * 31; }";
  helpers.close();

  // Create modules that reference shared
  for (const auto &mod : {"array", "map"}) {
    auto mod_dir = pkg_dir / mod;
    fs::create_directories(mod_dir);

    std::ofstream fal(mod_dir / (std::string(mod) + ".fal"));
    fal << "import \"../shared/helpers.fal\"\n";
    fal << "routine " << mod << "_routine() {}";
    fal.close();

    std::ofstream wrapper(mod_dir / (std::string(mod) + "-wrapper.so"));
    wrapper << "dummy";
    wrapper.close();
  }

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  // Resolve array module and shared
  auto importing = tmp_ / "main.fal";
  auto array_resolved = resolver.resolve("./collections/array", importing);
  auto helpers_resolved =
      resolver.resolve("./collections/shared/helpers.fal", importing);

  EXPECT_EQ(array_resolved.module_name, "array");
  EXPECT_EQ(helpers_resolved.module_name, "helpers");
}

// ============================================================================
// Test: Package Info Metadata
// ============================================================================

TEST_F(MultiModulePackageTest, MultiModulePackageMetadata) {
  create_multimodule_package(tmp_ / "collections", "collections", "1.0.0",
                             {"array", "map", "set"});

  auto pkg = falcon::pm::PackageResolver::is_package(tmp_ / "collections");
  EXPECT_TRUE(pkg);

  auto manifest =
      falcon::pm::PackageManifest::load(tmp_ / "collections" / "falcon.yml");
  EXPECT_EQ(manifest.name, "collections");
  EXPECT_EQ(manifest.version, "1.0.0");
}
