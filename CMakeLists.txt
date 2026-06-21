cmake_minimum_required(VERSION 3.20)
project(protectionstones LANGUAGES CXX)

# EndStone plugins must be built in Release/RelWithDebInfo (never MSVC Debug),
# and target C++20 to match the EndStone API.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

# The plugin is a shared library, so every dependency we statically link in
# (fmt, etc.) must also be compiled position-independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# --- EndStone v0.11.4 --------------------------------------------------------
# We only need the header-only API target (`endstone::endstone`) and the
# `endstone_add_plugin()` helper, both defined in the repo's `include/` subdir.
# SOURCE_SUBDIR keeps us from configuring the whole server build.
FetchContent_Declare(
        endstone
        GIT_REPOSITORY https://github.com/EndstoneMC/endstone.git
        GIT_TAG v0.11.4
        SOURCE_SUBDIR include
)
FetchContent_MakeAvailable(endstone)

# --- nlohmann/json (claim persistence) --------------------------------------
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# --- The plugin --------------------------------------------------------------
# endstone_add_plugin() creates a SHARED library prefixed with "endstone_"
# (so the output is endstone_protectionstones.so) and links endstone::endstone.
endstone_add_plugin(protectionstones
        src/plugin.cpp
        src/claim_manager.cpp
        src/protection_listener.cpp
        src/ui_manager.cpp
        src/lang.cpp
)

target_include_directories(protectionstones PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(protectionstones PRIVATE nlohmann_json::nlohmann_json)
