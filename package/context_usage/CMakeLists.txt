if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(context_usage
    src/context_usage/context_usage.cpp
    src/context_usage/context_usage.h
)

add_library(context_usage::context_usage ALIAS context_usage)

target_compile_features(context_usage PUBLIC cxx_std_20)

target_include_directories(context_usage
    PUBLIC
        include
        src
)

target_link_libraries(context_usage
    PUBLIC
        nlohmann_json::nlohmann_json
)
