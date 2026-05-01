#include "falcon-package-manager/PackageResolver.hpp"
#include "falcon-package-manager/PackageCache.hpp"
#include "falcon-package-manager/PackageManifest.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
using json = nlohmann::json;
using namespace falcon::pm;

namespace {

PackageResolver::ResolvedImport
resolve_as_package(const std::filesystem::path &abs) {
  std::filesystem::path main_path;
  try {
    main_path = abs / PackageResolver::get_package_main_file(abs);
  } catch (...) {
    throw std::runtime_error("No .fal files found in package: " + abs.string());
  }
  return PackageResolver::ResolvedImport{main_path,           main_path, abs,
                                         abs.stem().string(), "",        true};
}

PackageResolver::ResolvedImport
resolve_as_file(const std::filesystem::path &abs) {
  auto package_root = PackageManifest::find_root(abs.parent_path());
  std::string sha = PackageCache::sha256_file(abs);
  if (package_root) {
    return PackageResolver::ResolvedImport{
        abs, abs, *package_root, abs.stem().string(), sha, true};
  } else {
    return PackageResolver::ResolvedImport{
        abs, abs, abs.parent_path(), abs.stem().string(), sha, false};
  }
}

PackageResolver::ResolvedImport
resolve_as_module_dir(const std::filesystem::path &abs,
                      const std::filesystem::path &package_root) {
  auto rel_path = std::filesystem::relative(abs, package_root);
  std::string module_subpath = rel_path.string();
  std::filesystem::path main_path;
  try {
    std::string main_file =
        PackageResolver::get_package_main_file(package_root, module_subpath);
    main_path = package_root / main_file;
  } catch (...) {
    std::vector<std::filesystem::path> fal_files;
    for (const auto &entry : std::filesystem::directory_iterator(abs)) {
      if (entry.is_regular_file() && entry.path().extension() == ".fal") {
        fal_files.push_back(entry.path());
      }
    }
    if (fal_files.size() == 1) {
      main_path = fal_files[0];
    } else if (fal_files.size() > 1) {
      throw std::runtime_error("Ambiguous module directory " + abs.string() +
                               " contains multiple .fal files");
    } else {
      throw std::runtime_error("No .fal file found for module: " +
                               abs.string());
    }
  }
  std::string sha = PackageCache::sha256_file(main_path);
  return PackageResolver::ResolvedImport{
      main_path, main_path, package_root, main_path.stem().string(), sha, true};
}

} // namespace

std::tuple<int, int, int>
PackageResolver::parse_semver(const std::string &version_str) {
  std::string version = version_str;

  // Remove leading 'v' if present
  if (!version.empty() && version[0] == 'v') {
    version = version.substr(1);
  }

  std::istringstream iss(version);
  std::string token;
  std::vector<int> parts;

  while (std::getline(iss, token, '.')) {
    try {
      parts.push_back(std::stoi(token));
    } catch (...) {
      throw std::runtime_error("Invalid semantic version format: " +
                               version_str);
    }
  }

  if (parts.size() != 3) {
    throw std::runtime_error(
        "Semantic version must have 3 components (major.minor.patch): " +
        version_str);
  }

  return std::make_tuple(parts[0], parts[1], parts[2]);
}

std::vector<std::string>
PackageResolver::fetch_github_releases(const std::string &owner,
                                       const std::string &repo) {

  std::string url = "https://api.github.com/repos/" + owner + "/" + repo +
                    "/releases?per_page=100";

  std::string response;
  try {
    response = http_get(url);
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] Failed to fetch releases for " << owner << "/" << repo
              << ": " << e.what() << std::endl;
    throw std::runtime_error("Failed to fetch releases for " + owner + "/" +
                             repo + ": " + std::string(e.what()));
  }

  std::vector<std::string> tags;
  try {
    auto releases = json::parse(response);

    if (!releases.is_array()) {
      throw std::runtime_error("Expected JSON array from GitHub API");
    }

    for (const auto &release : releases) {
      if (release.contains("tag_name") && !release["tag_name"].is_null()) {
        tags.push_back(release["tag_name"].get<std::string>());
      }
    }
  } catch (const json::exception &e) {
    std::cerr << "[ERROR] Failed to parse GitHub API response: " << e.what()
              << std::endl;
    throw std::runtime_error("Failed to parse GitHub API response: " +
                             std::string(e.what()));
  }

  if (tags.empty()) {
    std::cerr << "[WARN] No releases found for " << owner << "/" << repo
              << std::endl;
    throw std::runtime_error("No releases found for " + owner + "/" + repo);
  }

  std::cerr << "[INFO] Found " << tags.size() << " releases for " << owner
            << "/" << repo << std::endl;
  return tags;
}

std::string PackageResolver::resolve_version_constraint(
    const std::string &owner, const std::string &repo,
    const std::string &version_constraint) {

  // If constraint is empty or "*", use latest
  if (version_constraint.empty() || version_constraint == "*") {
    auto tags = fetch_github_releases(owner, repo);
    return tags[0]; // First tag is latest (API returns in descending order)
  }

  // If it starts with 'v' or is a pure number, it's an exact version
  if ((version_constraint[0] == 'v') || std::isdigit(version_constraint[0])) {
    // Exact version match - add 'v' prefix if not present
    std::string exact_tag = version_constraint;
    if (exact_tag[0] != 'v') {
      exact_tag = "v" + exact_tag;
    }
    return exact_tag;
  }

  // Parse the constraint prefix
  char prefix = version_constraint[0];
  std::string constraint_version = version_constraint.substr(1);

  auto [major, minor, patch] = parse_semver(constraint_version);

  auto tags = fetch_github_releases(owner, repo);

  // Find the first (highest version) tag that matches the constraint
  for (const auto &tag : tags) {
    try {
      auto [tag_major, tag_minor, tag_patch] = parse_semver(tag);

      if (prefix == '^') {
        // Caret: compatible with X.Y.Z (allows changes that don't modify major
        // version)
        if (tag_major == major && tag_minor >= minor && tag_patch >= patch) {
          return tag;
        }
      } else if (prefix == '~') {
        // Tilde: compatible with X.Y.* (allows patch-level changes only)
        if (tag_major == major && tag_minor == minor && tag_patch >= patch) {
          return tag;
        }
      } else {
        throw std::runtime_error("Unknown version constraint prefix: " +
                                 std::string(1, prefix));
      }
    } catch (const std::runtime_error &) {
      // Skip tags that can't be parsed as semver
      continue;
    }
  }

  throw std::runtime_error("No release found matching constraint " +
                           version_constraint + " for " + owner + "/" + repo);
}

PackageResolver::GitHubURL
PackageResolver::parse_github_url(const std::string &url_string) {
  if (!url_string.starts_with("github.com/")) {
    throw std::runtime_error("Invalid GitHub URL: " + url_string);
  }

  std::string url = url_string.substr(11);
  GitHubURL result;
  result.branch =
      "main"; // Default branch (not used in v2, but kept for compat)

  std::istringstream iss(url);
  std::string token;
  std::vector<std::string> parts;

  while (std::getline(iss, token, '/')) {
    if (!token.empty()) {
      parts.push_back(token);
    }
  }

  if (parts.size() < 2) {
    throw std::runtime_error("GitHub URL must have at least owner/repo: " +
                             url_string);
  }

  result.owner = parts[0];
  std::string repo_and_branch = parts[1];

  size_t at_pos = repo_and_branch.find('@');
  if (at_pos != std::string::npos) {
    result.repo = repo_and_branch.substr(0, at_pos);
    result.branch = repo_and_branch.substr(at_pos + 1);
  } else {
    result.repo = repo_and_branch;
  }

  for (size_t i = 2; i < parts.size(); ++i) {
    if (i > 2)
      result.path += "/";
    result.path += parts[i];
  }

  return result;
}

std::pair<std::string, std::string>
PackageResolver::find_package_root_in_path(const GitHubURL &url) {
  std::string package_root = url.path;
  std::string subpath = "";

  if (package_root.ends_with(".fal")) {
    size_t last_slash = package_root.find_last_of('/');
    if (last_slash != std::string::npos) {
      subpath = package_root.substr(last_slash + 1);
      package_root = package_root.substr(0, last_slash);
    }
  }

  return {package_root, subpath};
}

std::filesystem::path PackageResolver::download_github_package(
    const std::string &owner, const std::string &repo,
    const std::string &version, const std::string &package_dir) {

  auto repo_cache_dir =
      cache_.cache_dir() / (repo + (version.empty() ? "" : "_" + version));

  if (!std::filesystem::exists(repo_cache_dir)) {
    std::cerr << "[INFO] Downloading package " << owner << "/" << repo
              << (version.empty() ? " (latest)" : " at version " + version)
              << std::endl;

    std::filesystem::create_directories(repo_cache_dir);
    std::filesystem::path temp_tar =
        cache_.cache_dir() / (repo + "_temp.tar.gz");

    // Construct the release download URL
    std::string tar_url;
    if (version.empty() || version == "latest") {
      tar_url = "https://github.com/" + owner + "/" + repo +
                "/releases/latest/download/" + repo + ".tar.gz";
    } else {
      tar_url = "https://github.com/" + owner + "/" + repo +
                "/releases/download/" + version + "/" + repo + ".tar.gz";
    }

    std::string dl_cmd =
        "curl -sL --fail -o \"" + temp_tar.string() + "\" \"" + tar_url + "\"";

    int ret = system(dl_cmd.c_str());
    if (ret == 0) {
      std::cerr << "[INFO] Package downloaded and extracting..." << std::endl;
      std::string extract_cmd = "tar -xzf \"" + temp_tar.string() + "\" -C \"" +
                                repo_cache_dir.string() + "\"";
      system(extract_cmd.c_str());
    } else {
      std::cerr << "[WARN] Failed to download release, trying source archive"
                << std::endl;
      tar_url = "https://github.com/" + owner + "/" + repo +
                "/archive/refs/tags/" + version + ".tar.gz";

      dl_cmd = "curl -sL --fail -o \"" + temp_tar.string() + "\" \"" + tar_url +
               "\"";
      ret = system(dl_cmd.c_str());

      if (ret == 0) {
        std::cerr << "[INFO] Source archive downloaded and extracting..."
                  << std::endl;
        std::string extract_cmd = "tar -xzf \"" + temp_tar.string() +
                                  "\" --strip-components=1 -C \"" +
                                  repo_cache_dir.string() + "\"";
        system(extract_cmd.c_str());
      } else {
        std::cerr << "[ERROR] Failed to download package from " << tar_url
                  << std::endl;
        std::filesystem::remove_all(repo_cache_dir);
        if (std::filesystem::exists(temp_tar))
          std::filesystem::remove(temp_tar);
        throw std::runtime_error("Failed to download package from " + tar_url);
      }
    }
    if (std::filesystem::exists(temp_tar))
      std::filesystem::remove(temp_tar);
  }

  auto final_path = repo_cache_dir;
  if (!package_dir.empty() && package_dir != "/") {
    final_path = repo_cache_dir / package_dir;
  }

  if (!std::filesystem::exists(final_path / "falcon.yml")) {
    throw std::runtime_error(
        "No falcon.yml found in downloaded package directory: " +
        final_path.string());
  }

  auto manifest = PackageManifest::load(final_path / "falcon.yml");
  for (const auto &[so_name, expected_hash] : manifest.ffi) {
    if (expected_hash.starts_with("sha256:")) {
      auto so_path = final_path / so_name;
      if (!std::filesystem::exists(so_path)) {
        throw std::runtime_error("Missing expected FFI binary in package: " +
                                 so_name);
      }
      std::string actual_hash = "sha256:" + PackageCache::sha256_file(so_path);
      if (actual_hash != expected_hash) {
        throw std::runtime_error("Security Error! Hash mismatch for " +
                                 so_name + "\nExpected: " + expected_hash +
                                 "\nActual:   " + actual_hash);
      }
    }
  }

  return final_path;
}

std::string PackageResolver::get_package_main_file(
    const std::filesystem::path &package_root,
    const std::string &module_subpath) {

  // Determine where to look for the main file
  std::filesystem::path search_dir = package_root;
  std::string module_name = module_subpath;

  if (!module_subpath.empty()) {
    search_dir = package_root / module_subpath;
    // Extract the last component of the subpath as module name
    size_t last_slash = module_subpath.find_last_of('/');
    if (last_slash != std::string::npos) {
      module_name = module_subpath.substr(last_slash + 1);
    }
  }

  // Strategy 1: Look for {module_name}.fal in the search directory
  if (std::filesystem::exists(search_dir / (module_name + ".fal"))) {
    return (module_subpath.empty() ? "" : module_subpath + "/") + module_name +
           ".fal";
  }

  // Strategy 2: If module_subpath is specified, look for subdir/module_name.fal
  if (!module_subpath.empty()) {
    auto nested_path = search_dir / module_name;
    if (std::filesystem::is_directory(nested_path) &&
        std::filesystem::exists(nested_path / (module_name + ".fal"))) {
      return module_subpath + "/" + module_name + "/" + module_name + ".fal";
    }
  }

  // Strategy 3: For single-module packages, try package name
  if (module_subpath.empty()) {
    auto manifest_file = package_root / "falcon.yml";
    if (std::filesystem::exists(manifest_file)) {
      try {
        auto manifest = PackageManifest::load(manifest_file);
        std::string pkg_name = manifest.name;
        if (!pkg_name.empty() &&
            std::filesystem::exists(package_root / (pkg_name + ".fal"))) {
          return pkg_name + ".fal";
        }
      } catch (...) {
      }
    }
  }

  // Strategy 4: Search for any .fal file in the directory
  if (std::filesystem::exists(search_dir)) {
    std::vector<std::filesystem::path> fal_files;
    for (const auto &entry : std::filesystem::directory_iterator(search_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".fal") {
        fal_files.push_back(entry.path());
      }
    }

    if (fal_files.size() == 1) {
      auto relative = fal_files[0].filename().string();
      if (module_subpath.empty()) {
        return relative;
      } else {
        return module_subpath + "/" + relative;
      }
    } else if (fal_files.size() > 1) {
      throw std::runtime_error(
          "Ambiguous module directory: " + search_dir.string() +
          " contains multiple .fal files. Please specify the exact file path.");
    }
  }

  // No .fal file found
  if (module_subpath.empty()) {
    throw std::runtime_error("No main .fal file found in package: " +
                             package_root.string());
  } else {
    throw std::runtime_error("No .fal file found for module '" +
                             module_subpath +
                             "' in package: " + package_root.string());
  }
}

bool PackageResolver::is_package(const std::filesystem::path &path) {
  if (std::filesystem::is_directory(path)) {
    return std::filesystem::exists(path / "falcon.yml");
  }
  return false;
}

PackageResolver::PackageResolver(
    std::filesystem::path project_root, PackageCache &cache,
    std::vector<std::filesystem::path> global_search_paths)
    : project_root_(std::move(project_root)),
      search_paths_(std::move(global_search_paths)), cache_(cache) {}

PackageResolver::ResolvedImport
PackageResolver::resolve(const std::string &import_path,
                         const std::filesystem::path &importing_file) {
  auto base_dir = importing_file.parent_path();

  if (import_path.starts_with("./") || import_path.starts_with("../")) {
    return resolve_local(std::filesystem::path(import_path), base_dir);
  }
  if (import_path.starts_with("github.com/")) {
    return resolve_github_package(import_path);
  }

  auto local_candidate = base_dir / import_path;
  if (std::filesystem::exists(local_candidate)) {
    return resolve_local(std::filesystem::path("./" + import_path), base_dir);
  }

  auto root_candidate = project_root_ / import_path;
  if (std::filesystem::exists(root_candidate)) {
    return resolve_local(import_path, project_root_);
  }

  for (const auto &path : search_paths_) {
    auto candidate = path / import_path;
    if (std::filesystem::exists(candidate)) {
      return resolve_local(import_path, path);
    }
  }

  throw std::runtime_error("Cannot resolve import '" + import_path + "'");
}

std::vector<std::filesystem::path>
PackageResolver::discover_packages(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> results;
  if (!std::filesystem::exists(root))
    return results;

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_directory() &&
        std::filesystem::exists(entry.path() / "falcon.yml")) {
      results.push_back(entry.path());
    }
  }
  return results;
}

std::vector<PackageResolver::ResolvedImport>
PackageResolver::resolve_all(const std::vector<std::string> &import_paths,
                             const std::filesystem::path &importing_file) {
  std::vector<ResolvedImport> results;
  results.reserve(import_paths.size());
  for (const auto &p : import_paths) {
    results.push_back(resolve(p, importing_file));
  }
  return results;
}

PackageResolver::ResolvedImport
PackageResolver::resolve_local(const std::filesystem::path &raw,
                               const std::filesystem::path &base_dir) {
  auto abs = std::filesystem::weakly_canonical(base_dir / raw);

  if (is_package(abs)) {
    return resolve_as_package(abs);
  }

  if (std::filesystem::is_regular_file(abs) && abs.extension() == ".fal") {
    return resolve_as_file(abs);
  }

  auto package_root = PackageManifest::find_root(abs);
  if (package_root && std::filesystem::is_directory(abs)) {
    return resolve_as_module_dir(abs, *package_root);
  }

  if (!std::filesystem::exists(abs)) {
    throw std::runtime_error("Import not found: " + abs.string());
  }

  std::string sha = PackageCache::sha256_file(abs);
  return PackageResolver::ResolvedImport{
      abs, abs, abs.parent_path(), abs.stem().string(), sha, false};
}

PackageResolver::ResolvedImport
PackageResolver::resolve_github_package(const std::string &import_path) {
  auto github_url = parse_github_url(import_path);
  auto [package_dir, subpath] = find_package_root_in_path(github_url);

  std::string version_tag = github_url.branch;
  if (version_tag != "main") {
    // Version was explicitly specified via @ syntax
  } else {
    version_tag = "";
  }

  auto pkg_root = download_github_package(github_url.owner, github_url.repo,
                                          version_tag, package_dir);

  resolve_package_dependencies(pkg_root);

  std::filesystem::path file_abs_path;
  std::string module_name;

  if (subpath.empty()) {
    // No subpath specified - try to find main file
    std::string main_file = get_package_main_file(pkg_root);
    file_abs_path = pkg_root / main_file;
    module_name = std::filesystem::path(main_file).stem().string();
  } else if (subpath.ends_with(".fal")) {
    // Explicit .fal file specified
    file_abs_path = pkg_root / subpath;
    module_name = std::filesystem::path(subpath).stem().string();
  } else {
    // Module subpath specified (e.g., "array")
    // Find the .fal file for this module
    std::string main_file = get_package_main_file(pkg_root, subpath);
    file_abs_path = pkg_root / main_file;
    module_name = std::filesystem::path(main_file).stem().string();
  }

  return ResolvedImport{file_abs_path, file_abs_path, pkg_root, module_name, "",
                        true};
}

void PackageResolver::resolve_package_dependencies(
    const std::filesystem::path &pkg_root) {
  auto manifest_path = pkg_root / "falcon.yml";
  if (!std::filesystem::exists(manifest_path))
    return;

  auto manifest = PackageManifest::load(manifest_path);

  if (manifest.dependencies.empty()) {
    return;
  }

  for (const auto &dep : manifest.dependencies) {
    try {
      std::string import_path;

      if (dep.github) {
        import_path = *dep.github;

        // If a version constraint is specified, resolve it to a tag
        if (!dep.version.empty() && dep.version != "*") {
          try {
            // Try to parse owner/repo from the import path
            if (import_path.find("github.com/") != std::string::npos) {
              auto dep_url = parse_github_url(import_path);

              // Resolve the version constraint to a specific tag
              std::string resolved_tag = resolve_version_constraint(
                  dep_url.owner, dep_url.repo, dep.version);

              std::cerr << "[INFO] Resolved dependency '" << dep.name
                        << "' version constraint '" << dep.version
                        << "' to tag: " << resolved_tag << std::endl;

              // Update import path to include the resolved version
              import_path = "github.com/" + dep_url.owner + "/" + dep_url.repo +
                            "@" + resolved_tag;

              if (!dep_url.path.empty()) {
                import_path += "/" + dep_url.path;
              }
            }
          } catch (const std::exception &e) {
            std::cerr << "[WARN] Failed to resolve version constraint '"
                      << dep.version << "' for dependency '" << dep.name
                      << "': " << e.what() << " (skipping version resolution)"
                      << std::endl;
            // Continue with the original import path (may use source archive)
          }
        }
      } else if (dep.local_path) {
        import_path = *dep.local_path;
      } else {
        std::cerr << "[WARN] Dependency '" << dep.name
                  << "' has neither github nor local_path specified"
                  << std::endl;
        continue;
      }

      resolve(import_path, pkg_root / "dummy.fal");

    } catch (const std::exception &e) {
      // Dependencies are optional - log but don't fail
      std::cerr << "[WARN] Failed to resolve optional dependency '" << dep.name
                << "': " << e.what() << std::endl;
    }
  }
}
std::string PackageResolver::http_get(const std::string &url) {
  std::filesystem::path temp_response =
      std::filesystem::temp_directory_path() / "curl_response.json";

  std::string curl_cmd = "curl -sL \"" + url + "\" -o \"" +
                         temp_response.string() + "\" 2>/dev/null";

  int ret = system(curl_cmd.c_str());
  if (ret != 0) {
    if (std::filesystem::exists(temp_response))
      std::filesystem::remove(temp_response);
    throw std::runtime_error("HTTP GET failed for URL: " + url);
  }

  if (!std::filesystem::exists(temp_response)) {
    throw std::runtime_error("Response file not created for URL: " + url);
  }

  std::string response;
  try {
    std::ifstream file(temp_response);
    if (!file.is_open()) {
      throw std::runtime_error("Cannot open response file: " +
                               temp_response.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    response = buffer.str();
    file.close();
  } catch (const std::exception &e) {
    std::filesystem::remove(temp_response);
    throw std::runtime_error("Failed to read response: " +
                             std::string(e.what()));
  }

  std::filesystem::remove(temp_response);
  return response;
}
