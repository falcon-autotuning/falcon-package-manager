#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace falcon::pm {

class PackageCache;

/**
 * @brief Resolves an import path string into an absolute filesystem path.
 *
 * Supports:
 *  - Relative local paths:   "./Quantity.fal"  "./subdir/Adder.fal"
 *  - Local packages:         "../shared/array" (directory with falcon.yml)
 *  - GitHub packages:        "github.com/owner/repo/libs/collections/array"
 *  - Version-pinned imports: "github.com/owner/repo@v1.2.3"
 *  - Multi-module imports:   "github.com/owner/repo/array" (resolves to
 * array.fal or array/array.fal)
 *
 * A Falcon package is identified by a `falcon.yml` metadata file in its root
 * directory.
 *
 * Version Resolution:
 *  When resolving dependencies from falcon.yml, the package manager respects
 *  SemVer constraints (^1.0.0, ~2.1.0, 1.2.3, *). If a constraint is provided,
 *  it queries GitHub releases and selects the highest matching version.
 *  If no version is specified or constraint is "*", the latest release is used.
 *
 * Multi-Module Packages:
 *  Packages can contain multiple modules in subdirectories. The resolver will:
 *  1. Look for module_name.fal in the subdir
 *  2. Look for subdir/module_name.fal
 *  3. Look for any .fal file in the subdir
 *  4. Return a helpful error if ambiguous or not found
 */
class PackageResolver {
public:
  PackageResolver(std::filesystem::path project_root, PackageCache &cache,
                  std::vector<std::filesystem::path> global_search_paths = {});

  struct ResolvedImport {
    std::filesystem::path absolute_path; ///< Absolute path to the .fal file
    std::filesystem::path cached_path;   ///< Path inside .falcon/cache/
    std::filesystem::path
        package_root; ///< Root of the package (for FFI/relative resolution)
    std::string module_name; ///< e.g. "array" from package name
    std::string sha256;      ///< SHA-256 of the resolved file
    bool is_package;         ///< True if this is a package import
  };

  /**
   * @brief Resolve one import path string.
   *
   * @param import_path  The raw string from `import "..."` in the .fal source.
   * @param importing_file  Absolute path of the file containing the import.
   * @return Resolved import information
   * @throws std::runtime_error if the path cannot be resolved or downloaded.
   */
  ResolvedImport resolve(const std::string &import_path,
                         const std::filesystem::path &importing_file);

  /**
   * @brief Resolve all imports declared in a .fal source's import list.
   */
  std::vector<ResolvedImport>
  resolve_all(const std::vector<std::string> &import_paths,
              const std::filesystem::path &importing_file);

  /**
   * @brief Detect if a path is a Falcon package.
   *
   * A Falcon package must have a falcon.yml file at its root.
   *
   * @param path Local filesystem path or GitHub URL
   * @return True if the path is a Falcon package
   */
  static bool is_package(const std::filesystem::path &path);

  /**
   * @brief Recursively discover all Falcon packages within a directory tree.
   * @param root The directory to start the search from.
   * @return Vector of absolute paths to package roots.
   */
  static std::vector<std::filesystem::path>
  discover_packages(const std::filesystem::path &root);
  /**
   * @brief Find the main .fal file for a module within a package.
   *
   * For single-module packages, finds the main .fal file.
   * For multi-module packages with a subpath, finds the module's .fal file.
   *
   * Supports patterns:
   *  - module_name.fal (in package root)
   *  - module_subdir/module_name.fal
   *  - module_subdir/subdir.fal (any .fal in the subdir)
   *
   * @param package_root Root of the package
   * @param module_subpath Optional subpath to a module (e.g., "array")
   * @return Filename relative to package_root
   * @throws std::runtime_error if no .fal file is found
   */
  static std::string
  get_package_main_file(const std::filesystem::path &package_root,
                        const std::string &module_subpath = "");

private:
  /**
   * @brief Recursively resolve and cache all dependencies of a package.
   *
   * When a package is resolved, we need to also resolve and cache all of its
   * dependencies (and their dependencies, recursively) so that FFI wrappers
   * and .fal files from the entire dependency tree are available.
   *
   * Version constraints from falcon.yml are resolved to specific release tags.
   */
  void resolve_package_dependencies(const std::filesystem::path &pkg_root);

  struct GitHubURL {
    std::string owner;
    std::string repo;
    std::string branch;
    std::string path;
  };

  ResolvedImport resolve_local(const std::filesystem::path &raw,
                               const std::filesystem::path &base_dir);

  ResolvedImport resolve_github_package(const std::string &import_path);

  static GitHubURL parse_github_url(const std::string &url_string);

  /**
   * @brief Find the falcon.yml file in the GitHub path hierarchy.
   *
   * Walks up the directory tree to find the falcon.yml that identifies the
   * package.
   *
   * @return Pair of (package_directory, remaining_subpath)
   */
  static std::pair<std::string, std::string>
  find_package_root_in_path(const GitHubURL &url);

  std::filesystem::path download_github_package(const std::string &owner,
                                                const std::string &repo,
                                                const std::string &version,
                                                const std::string &package_dir);

  static std::string http_get(const std::string &url);

  /**
   * @brief Resolve a SemVer version constraint to a specific GitHub release
   * tag.
   *
   * Supports constraints:
   *  - "1.2.3"       Exact version
   *  - "^1.2.3"      Caret: compatible with 1.2.3 (allows minor/patch changes)
   *  - "~1.2.3"      Tilde: compatible with 1.2.x (allows patch changes only)
   *  - "*"           Latest release
   *  - ""            Latest release (default)
   *
   * @param owner GitHub owner
   * @param repo Repository name
   * @param version_constraint SemVer constraint
   * @return The matching release tag (e.g., "v1.2.3")
   * @throws std::runtime_error if no matching release is found
   */
  static std::string
  resolve_version_constraint(const std::string &owner, const std::string &repo,
                             const std::string &version_constraint);

  /**
   * @brief Fetch all release tags from a GitHub repository.
   *
   * Queries the GitHub API to retrieve all published releases, returned in
   * descending version order (newest first).
   *
   * @param owner GitHub owner
   * @param repo Repository name
   * @return Vector of release tags (e.g., ["v1.2.3", "v1.2.2", "v1.2.1"])
   * @throws std::runtime_error if the API call fails
   */
  static std::vector<std::string>
  fetch_github_releases(const std::string &owner, const std::string &repo);

  /**
   * @brief Parse a semantic version string (e.g., "1.2.3") into components.
   *
   * @param version_str Version string (with or without leading "v")
   * @return Tuple of (major, minor, patch)
   * @throws std::runtime_error if the version format is invalid
   */
  static std::tuple<int, int, int> parse_semver(const std::string &version_str);

  std::filesystem::path project_root_;
  std::vector<std::filesystem::path> search_paths_;
  PackageCache &cache_;
};

} // namespace falcon::pm
