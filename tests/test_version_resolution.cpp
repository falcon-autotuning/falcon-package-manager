#include "falcon-package-manager/PackageCache.hpp"
#include "falcon-package-manager/PackageManifest.hpp"
#include "falcon-package-manager/PackageResolver.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

/**
 * @brief Mock HTTP responses for GitHub API calls.
 *
 * This helper class provides mock GitHub API responses for testing
 * version resolution without requiring real network access.
 */
class MockGitHubAPI {
public:
  static json create_release(const std::string &tag, const std::string &name) {
    return json{
        {"tag_name", tag},
        {"name", name},
        {"draft", false},
        {"prerelease", false},
        {"created_at", "2024-01-01T00:00:00Z"},
        {"assets", json::array({{
                       {"name", "package.tar.gz"},
                       {"browser_download_url",
                        "https://github.com/owner/repo/releases/download/" +
                            tag + "/package.tar.gz"},
                   }})},
    };
  }

  static std::string releases_response(const std::vector<std::string> &tags) {
    json releases = json::array();
    for (const auto &tag : tags) {
      releases.push_back(create_release(tag, "Release " + tag));
    }
    return releases.dump();
  }
};

class VersionResolutionTest : public ::testing::Test {
protected:
  fs::path tmp_;

  void SetUp() override {
    tmp_ = fs::temp_directory_path() / "falcon_pm_version_test";
    fs::create_directories(tmp_);
  }

  void TearDown() override { fs::remove_all(tmp_); }

  /**
   * @brief Create a mock falcon.yml with version info
   */
  void create_manifest(const fs::path &dir, const std::string &name,
                       const std::string &version,
                       const std::vector<std::string> &deps = {}) {
    fs::create_directories(dir);

    std::string manifest_content = R"(
name: )" + name + R"(
version: )" + version + R"(
maintainer: Test Author
github: owner/)" + name + R"(
license: MPL-2.0
ffi: {}
dependencies:
)";

    for (const auto &dep : deps) {
      manifest_content += "  - name: " + dep + "\n";
      manifest_content += "    version: ^1.0.0\n";
      manifest_content += "    github: owner/" + dep + "\n";
    }

    std::ofstream f(dir / "falcon.yml");
    f << manifest_content;
  }

  void create_fal_file(const fs::path &dir, const std::string &name) {
    std::ofstream f(dir / (name + ".fal"));
    f << "routine " << name << "() {}";
  }

  /**
   * @brief Create a minimal package that can be discovered
   */
  void create_package(const fs::path &dir, const std::string &name,
                      const std::string &version) {
    create_manifest(dir, name, version);
    create_fal_file(dir, name);
  }
};

// ============================================================================
// Test: Package Detection and Discovery (Public API)
// ============================================================================

TEST_F(VersionResolutionTest, IsPackageDetectsValidPackage) {
  create_package(tmp_ / "test-pkg", "test-pkg", "1.0.0");

  EXPECT_TRUE(falcon::pm::PackageResolver::is_package(tmp_ / "test-pkg"));
}

TEST_F(VersionResolutionTest, IsPackageRejectsNonPackage) {
  fs::create_directories(tmp_ / "not-a-package");

  EXPECT_FALSE(falcon::pm::PackageResolver::is_package(tmp_ / "not-a-package"));
}

TEST_F(VersionResolutionTest, IsPackageRejectsNonexistent) {
  EXPECT_FALSE(falcon::pm::PackageResolver::is_package(tmp_ / "nonexistent"));
}

// ============================================================================
// Test: Recursive Package Discovery (Public API)
// ============================================================================

TEST_F(VersionResolutionTest, DiscoverPackagesEmpty) {
  auto discovered = falcon::pm::PackageResolver::discover_packages(tmp_);
  EXPECT_EQ(discovered.size(), 0);
}

TEST_F(VersionResolutionTest, DiscoverPackagesSingle) {
  create_package(tmp_ / "pkg1", "pkg1", "1.0.0");

  auto discovered = falcon::pm::PackageResolver::discover_packages(tmp_);
  ASSERT_EQ(discovered.size(), 1);
  EXPECT_EQ(fs::canonical(discovered[0]), fs::canonical(tmp_ / "pkg1"));
}

TEST_F(VersionResolutionTest, DiscoverPackagesMultiple) {
  create_package(tmp_ / "pkg1", "pkg1", "1.0.0");
  create_package(tmp_ / "pkg2", "pkg2", "1.0.0");
  create_package(tmp_ / "pkg3", "pkg3", "1.0.0");

  auto discovered = falcon::pm::PackageResolver::discover_packages(tmp_);
  ASSERT_EQ(discovered.size(), 3);
}

TEST_F(VersionResolutionTest, DiscoverPackagesRecursive) {
  create_package(tmp_ / "pkg1", "pkg1", "1.0.0");
  create_package(tmp_ / "libs" / "pkg2", "pkg2", "1.0.0");
  create_package(tmp_ / "libs" / "nested" / "pkg3", "pkg3", "1.0.0");

  auto discovered = falcon::pm::PackageResolver::discover_packages(tmp_);

  ASSERT_EQ(discovered.size(), 3);

  // Verify all packages are found (order may vary)
  std::set<fs::path> discovered_set(discovered.begin(), discovered.end());
  EXPECT_NE(discovered_set.find(tmp_ / "pkg1"), discovered_set.end());
  EXPECT_NE(discovered_set.find(tmp_ / "libs" / "pkg2"), discovered_set.end());
  EXPECT_NE(discovered_set.find(tmp_ / "libs" / "nested" / "pkg3"),
            discovered_set.end());
}

TEST_F(VersionResolutionTest, DiscoverPackagesIgnoresNonPackages) {
  create_package(tmp_ / "pkg1", "pkg1", "1.0.0");
  fs::create_directories(tmp_ / "not-a-package");

  auto discovered = falcon::pm::PackageResolver::discover_packages(tmp_);
  ASSERT_EQ(discovered.size(), 1);
}

// ============================================================================
// Test: Mock GitHub API Response Parsing
// ============================================================================

TEST_F(VersionResolutionTest, MockGitHubAPICreateRelease) {
  auto release = MockGitHubAPI::create_release("v1.2.3", "Release 1.2.3");

  EXPECT_EQ(release["tag_name"], "v1.2.3");
  EXPECT_EQ(release["name"], "Release 1.2.3");
  EXPECT_FALSE(release["draft"]);
  EXPECT_FALSE(release["prerelease"]);
  ASSERT_TRUE(release["assets"].is_array());
  ASSERT_GT(release["assets"].size(), 0);
  EXPECT_EQ(release["assets"][0]["name"], "package.tar.gz");
}

TEST_F(VersionResolutionTest, MockGitHubAPIReleasesResponse) {
  std::vector<std::string> tags = {"v2.0.0", "v1.2.3", "v1.2.2", "v1.0.0"};
  auto response = MockGitHubAPI::releases_response(tags);

  // Verify JSON can be parsed
  auto releases = json::parse(response);
  ASSERT_TRUE(releases.is_array());
  ASSERT_EQ(releases.size(), 4);

  // Verify tag extraction
  EXPECT_EQ(releases[0]["tag_name"], "v2.0.0");
  EXPECT_EQ(releases[1]["tag_name"], "v1.2.3");
  EXPECT_EQ(releases[2]["tag_name"], "v1.2.2");
  EXPECT_EQ(releases[3]["tag_name"], "v1.0.0");
}

TEST_F(VersionResolutionTest, MockGitHubAPIMultipleReleases) {
  std::vector<std::string> tags;
  for (int i = 0; i < 10; ++i) {
    tags.push_back("v1." + std::to_string(i) + ".0");
  }

  auto response = MockGitHubAPI::releases_response(tags);
  auto releases = json::parse(response);

  ASSERT_EQ(releases.size(), 10);
  for (size_t i = 0; i < tags.size(); ++i) {
    EXPECT_EQ(releases[i]["tag_name"], tags[i]);
  }
}

// ============================================================================
// Test: Manifest Parsing with Dependencies
// ============================================================================

TEST_F(VersionResolutionTest, CreateManifestNoDependencies) {
  create_manifest(tmp_ / "pkg1", "pkg1", "1.0.0");

  ASSERT_TRUE(fs::exists(tmp_ / "pkg1" / "falcon.yml"));

  std::ifstream f(tmp_ / "pkg1" / "falcon.yml");
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

  EXPECT_NE(content.find("name: pkg1"), std::string::npos);
  EXPECT_NE(content.find("version: 1.0.0"), std::string::npos);
}

TEST_F(VersionResolutionTest, CreateManifestWithDependencies) {
  create_manifest(tmp_ / "pkg1", "pkg1", "1.0.0", {"dep1", "dep2"});

  ASSERT_TRUE(fs::exists(tmp_ / "pkg1" / "falcon.yml"));

  std::ifstream f(tmp_ / "pkg1" / "falcon.yml");
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

  EXPECT_NE(content.find("name: pkg1"), std::string::npos);
  EXPECT_NE(content.find("version: 1.0.0"), std::string::npos);
  EXPECT_NE(content.find("name: dep1"), std::string::npos);
  EXPECT_NE(content.find("name: dep2"), std::string::npos);
}

// ============================================================================
// Test: Resolver Construction and Basic Operations
// ============================================================================

TEST_F(VersionResolutionTest, ResolverConstructionWithCache) {
  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  // Resolver should be constructed successfully
  EXPECT_TRUE(true);
}

TEST_F(VersionResolutionTest, ResolverWithSearchPaths) {
  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  std::vector<fs::path> search_paths = {tmp_ / "search1", tmp_ / "search2"};

  falcon::pm::PackageResolver resolver(tmp_, cache, search_paths);

  // Resolver should be constructed successfully with search paths
  EXPECT_TRUE(true);
}

// ============================================================================
// Test: Local File Resolution (Public resolve API)
// ============================================================================

TEST_F(VersionResolutionTest, ResolveLocalRelativeFile) {
  fs::create_directories(tmp_ / "src");
  create_fal_file(tmp_ / "src", "Adder");

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  auto importing = tmp_ / "src" / "main.fal";
  auto resolved = resolver.resolve("./Adder.fal", importing);

  EXPECT_EQ(resolved.module_name, "Adder");
  EXPECT_FALSE(resolved.sha256.empty());
  EXPECT_FALSE(resolved.is_package);
}

TEST_F(VersionResolutionTest, ResolveAllMultipleFiles) {
  fs::create_directories(tmp_ / "src");
  create_fal_file(tmp_ / "src", "Adder");
  create_fal_file(tmp_ / "src", "Multiplier");

  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  auto importing = tmp_ / "src" / "main.fal";
  auto results =
      resolver.resolve_all({"./Adder.fal", "./Multiplier.fal"}, importing);

  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].module_name, "Adder");
  EXPECT_EQ(results[1].module_name, "Multiplier");
}

// ============================================================================
// Test: Version Constraint Data Structures
// ============================================================================

TEST_F(VersionResolutionTest, ManifestLoadingSimple) {
  create_manifest(tmp_ / "pkg", "test-pkg", "1.0.0");

  auto manifest =
      falcon::pm::PackageManifest::load(tmp_ / "pkg" / "falcon.yml");

  EXPECT_EQ(manifest.name, "test-pkg");
  EXPECT_EQ(manifest.version, "1.0.0");
  EXPECT_EQ(manifest.maintainer, "Test Author");
}

TEST_F(VersionResolutionTest, ManifestLoadingWithDependencies) {
  create_manifest(tmp_ / "pkg", "test-pkg", "1.0.0", {"dep1", "dep2"});

  auto manifest =
      falcon::pm::PackageManifest::load(tmp_ / "pkg" / "falcon.yml");

  EXPECT_EQ(manifest.name, "test-pkg");
  ASSERT_EQ(manifest.dependencies.size(), 2u);
  EXPECT_EQ(manifest.dependencies[0].name, "dep1");
  EXPECT_EQ(manifest.dependencies[0].version, "^1.0.0");
  EXPECT_EQ(manifest.dependencies[1].name, "dep2");
  EXPECT_EQ(manifest.dependencies[1].version, "^1.0.0");
}

// ============================================================================
// Test: Cache Behavior
// ============================================================================

TEST_F(VersionResolutionTest, CacheCreation) {
  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);

  // Cache should be accessible even if directory doesn't exist yet
  EXPECT_EQ(cache.cache_dir(), cache_dir);
}

TEST_F(VersionResolutionTest, CacheClear) {
  auto cache_dir = tmp_ / ".cache";
  fs::create_directories(cache_dir);

  // Create a test file in cache
  std::ofstream test_file(cache_dir / "test.txt");
  test_file << "test content";
  test_file.close();

  ASSERT_TRUE(fs::exists(cache_dir / "test.txt"));

  falcon::pm::PackageCache cache(cache_dir);
  cache.clear();

  EXPECT_FALSE(fs::exists(cache_dir / "test.txt"));
}

// ============================================================================
// Test: Hash Computation
// ============================================================================

TEST_F(VersionResolutionTest, SHA256FileComputation) {
  auto test_file = tmp_ / "test.txt";
  std::ofstream f(test_file);
  f << "test content";
  f.close();

  auto hash = falcon::pm::PackageCache::sha256_file(test_file);

  // Should produce a valid 64-character hex string
  EXPECT_EQ(hash.length(), 64u);
  // Verify all characters are hex digits
  for (char c : hash) {
    EXPECT_TRUE(std::isxdigit(c)) << "Invalid hex character: " << c;
  }
}

TEST_F(VersionResolutionTest, SHA256StringComputation) {
  auto hash = falcon::pm::PackageCache::sha256_string("test content");

  // Should produce a valid 64-character hex string
  EXPECT_EQ(hash.length(), 64u);
  // Verify all characters are hex digits
  for (char c : hash) {
    EXPECT_TRUE(std::isxdigit(c)) << "Invalid hex character: " << c;
  }
}

TEST_F(VersionResolutionTest, SHA256Consistency) {
  auto hash1 = falcon::pm::PackageCache::sha256_string("content");
  auto hash2 = falcon::pm::PackageCache::sha256_string("content");

  EXPECT_EQ(hash1, hash2);
}

TEST_F(VersionResolutionTest, SHA256DifferenceDetection) {
  auto hash1 = falcon::pm::PackageCache::sha256_string("content1");
  auto hash2 = falcon::pm::PackageCache::sha256_string("content2");

  EXPECT_NE(hash1, hash2);
}

// ============================================================================
// Test: Error Handling
// ============================================================================

TEST_F(VersionResolutionTest, ResolveMissingFileThrows) {
  auto cache_dir = tmp_ / ".cache";
  falcon::pm::PackageCache cache(cache_dir);
  falcon::pm::PackageResolver resolver(tmp_, cache);

  auto importing = tmp_ / "main.fal";
  EXPECT_THROW(resolver.resolve("./NonExistent.fal", importing),
               std::runtime_error);
}

TEST_F(VersionResolutionTest, SHA256NonexistentFileThrows) {
  EXPECT_THROW(falcon::pm::PackageCache::sha256_file(tmp_ / "nonexistent.txt"),
               std::runtime_error);
}

// ============================================================================
// Test: Integration - Full Workflow
// ============================================================================

TEST_F(VersionResolutionTest, IntegrationDiscoverAndResolve) {
  // Create a small project structure
  create_package(tmp_ / "core", "core", "1.0.0");
  create_package(tmp_ / "libs" / "math", "math", "2.0.0");

  // Discover packages
  auto discovered = falcon::pm::PackageResolver::discover_packages(tmp_);
  ASSERT_EQ(discovered.size(), 2);

  // Verify both are packages
  for (const auto &pkg : discovered) {
    EXPECT_TRUE(falcon::pm::PackageResolver::is_package(pkg));
  }

  // Load and verify manifests
  for (const auto &pkg : discovered) {
    auto manifest = falcon::pm::PackageManifest::load(pkg / "falcon.yml");
    EXPECT_FALSE(manifest.name.empty());
    EXPECT_FALSE(manifest.version.empty());
  }
}

TEST_F(VersionResolutionTest, IntegrationMultiLevelDependencies) {
  // pkg1 depends on pkg2 which depends on pkg3
  create_manifest(tmp_ / "pkg1", "pkg1", "1.0.0", {"pkg2"});
  create_manifest(tmp_ / "pkg2", "pkg2", "1.0.0", {"pkg3"});
  create_manifest(tmp_ / "pkg3", "pkg3", "1.0.0");

  auto m1 = falcon::pm::PackageManifest::load(tmp_ / "pkg1" / "falcon.yml");
  auto m2 = falcon::pm::PackageManifest::load(tmp_ / "pkg2" / "falcon.yml");
  auto m3 = falcon::pm::PackageManifest::load(tmp_ / "pkg3" / "falcon.yml");

  EXPECT_EQ(m1.dependencies.size(), 1u);
  EXPECT_EQ(m2.dependencies.size(), 1u);
  EXPECT_EQ(m3.dependencies.size(), 0u);

  EXPECT_EQ(m1.dependencies[0].name, "pkg2");
  EXPECT_EQ(m2.dependencies[0].name, "pkg3");
}
