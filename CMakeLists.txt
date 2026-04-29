cmake_minimum_required(VERSION 3.20)
project(falcon-package-manager VERSION 1.0.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# ─── Library ─────────────────────────────────────────────────────────────────
add_library(falcon-package-manager STATIC
  src/PackageManifest.cpp
  src/PackageCache.cpp
  src/PackageResolver.cpp
  src/PackageManager.cpp
)

set_target_properties(falcon-package-manager PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(falcon-package-manager
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(falcon-package-manager
  PUBLIC
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    OpenSSL::Crypto
    CURL::libcurl
)

# ─── CLI binary ──────────────────────────────────────────────────────────────
add_executable(falcon-package-manager-cli src/main_cli.cpp)
target_link_libraries(falcon-package-manager-cli PRIVATE falcon-package-manager)
set_target_properties(falcon-package-manager-cli PROPERTIES OUTPUT_NAME falcon-pm)

# ─── Unit tests ──────────────────────────────────────────────────────────────
if(BUILD_TESTS)
  find_package(GTest CONFIG REQUIRED)
  enable_testing()

  add_executable(falcon-package-manager-tests
    tests/test_manifest.cpp
    tests/test_cache.cpp
    tests/test_resolver.cpp
    tests/test_package_manager.cpp
    tests/test_integration_github_packages.cpp
  )
  target_link_libraries(falcon-package-manager-tests
    PRIVATE falcon-package-manager GTest::gtest GTest::gtest_main
  )
  add_test(NAME falcon-package-manager-tests COMMAND falcon-package-manager-tests)

  # Integration tests (require network)
  add_executable(falcon-package-manager-integration-tests
    tests/test_integration_github_packages.cpp
  )
  target_link_libraries(falcon-package-manager-integration-tests
    PRIVATE falcon-package-manager GTest::gtest GTest::gtest_main
  )
  add_test(NAME falcon-package-manager-integration-tests COMMAND falcon-package-manager-integration-tests)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS falcon-package-manager
  EXPORT falcon-package-manager-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install CLI separately WITHOUT exporting it
install(TARGETS falcon-package-manager-cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/falcon-package-manager
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "*.h"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/falcon-package-manager-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(EXPORT falcon-package-manager-targets
  FILE      falcon-package-manager-targets.cmake
  NAMESPACE falcon::
  DESTINATION ${CMAKE_INSTALL_DATADIR}/falcon-package-manager
)
configure_package_config_file(
  cmake/falcon-package-manager-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/falcon-package-manager-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/falcon-package-manager
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/falcon-package-manager-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/falcon-package-manager-config-version.cmake"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/falcon-package-manager
)
