cmake_minimum_required(VERSION 3.20)
project(TerminalTutor VERSION 0.1.0 LANGUAGES CXX)

# =============================================================================
# Build Configuration
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
endif()

# =============================================================================
# Options
# =============================================================================
option(TT_BUILD_TESTS "Build unit tests" ON)

# =============================================================================
# FetchContent Dependencies
# =============================================================================
include(FetchContent)

# cpp-httplib - Header-only HTTP library
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
    GIT_SHALLOW TRUE
)
message(STATUS "Fetching cpp-httplib...")
FetchContent_MakeAvailable(httplib)

# nlohmann_json - JSON library
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
message(STATUS "Fetching nlohmann_json...")
FetchContent_MakeAvailable(json)

# =============================================================================
# Find System Dependencies
# =============================================================================
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# libsecret for secure credential storage
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECRET REQUIRED libsecret-1)

# =============================================================================
# Main Library
# =============================================================================
add_library(tt_core STATIC
    src/CommandParser.cpp
    src/GeminiClient.cpp
    src/ExplainerEngine.cpp
    src/Simulator.cpp
)

target_include_directories(tt_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tt_core PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    httplib::httplib
    nlohmann_json::nlohmann_json
)

# =============================================================================
# Main Executable
# =============================================================================
add_executable(tt src/main.cpp)
target_include_directories(tt PRIVATE ${LIBSECRET_INCLUDE_DIRS})
target_link_libraries(tt PRIVATE tt_core ${LIBSECRET_LIBRARIES})

# =============================================================================
# Tests
# =============================================================================
if(TT_BUILD_TESTS)
    enable_testing()
    
    add_executable(test_command_parser tests/test_command_parser.cpp)
    target_link_libraries(test_command_parser PRIVATE tt_core)
    add_test(NAME CommandParserTest COMMAND test_command_parser)
endif()

# =============================================================================
# Install
# =============================================================================
install(TARGETS tt DESTINATION bin)
install(DIRECTORY include/tt DESTINATION include)

# =============================================================================
# Uninstall
# =============================================================================
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_INSTALL_PREFIX}/bin/tt
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_INSTALL_PREFIX}/include/tt
    COMMENT "Uninstalling TerminalTutor..."
)

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "=== TerminalTutor Build Configuration ===")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests:    ${TT_BUILD_TESTS}")
message(STATUS "")
