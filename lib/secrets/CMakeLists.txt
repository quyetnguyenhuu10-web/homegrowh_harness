if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(secrets
    src/credential/credential.cpp
    src/credential/credential.h
    src/credential/validation.h
    src/memory/secure_string.cpp
    src/memory/secure_string.h
    src/memory/wipe.h
    src/error/error.h
)

add_library(secrets::secrets ALIAS secrets)

target_compile_features(secrets PUBLIC cxx_std_20)
target_link_libraries(secrets PUBLIC nlohmann_json::nlohmann_json)

target_include_directories(secrets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_sources(secrets PRIVATE
        src/platform/windows/credential/windows_credential.cpp
        src/platform/windows/credential/windows_credential.h
    )

    target_link_libraries(secrets PRIVATE Advapi32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(secrets PRIVATE
        src/platform/linux/credential/linux_credential.cpp
        src/platform/linux/credential/linux_credential.h
        src/platform/linux/credential/keyutils_api.cpp
        src/platform/linux/credential/keyutils_api.h
    )

    target_link_libraries(secrets PRIVATE ${CMAKE_DL_LIBS})
else()
    message(FATAL_ERROR
        "Unsupported operating system: ${CMAKE_SYSTEM_NAME}"
    )
endif()

option(SECRETS_BUILD_TESTS "Build secrets tests" OFF)

if(SECRETS_BUILD_TESTS)
    enable_testing()
    add_executable(secrets_credential_test
        tests/credential_test.cpp
    )
    target_link_libraries(secrets_credential_test PRIVATE secrets)

    # Run the Linux backend with deterministic API failures on either host OS.
    add_executable(secrets_linux_api_test
        tests/linux_api_test.cpp
        src/platform/linux/credential/linux_credential.cpp
        src/platform/linux/credential/keyutils_api.cpp
        src/memory/secure_string.cpp)
    target_include_directories(secrets_linux_api_test PRIVATE include src tests/fakes)
    target_compile_features(secrets_linux_api_test PRIVATE cxx_std_20)
    target_link_libraries(secrets_linux_api_test PRIVATE nlohmann_json::nlohmann_json)
    if(WIN32)
        target_compile_definitions(secrets_linux_api_test PRIVATE uid_t=unsigned ENOKEY=126)
    endif()
    add_test(NAME secrets_credential COMMAND secrets_credential_test)
    foreach(scenario IN ITEMS
        loader_failure symbol_failure storage_failure search_failure not_found
        read_failure read_data_failure zero_errno update_failure add_failure
        unlink_failure grow_read optional_fallback library_fallback)
        add_test(NAME secrets_linux_${scenario} COMMAND secrets_linux_api_test ${scenario})
    endforeach()
endif()
