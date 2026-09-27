if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(secrets
    src/credential/credential.cpp
    src/credential/credential.h
    src/memory/secure_string.cpp
    src/memory/secure_string.h
)

add_library(secrets::secrets ALIAS secrets)

target_compile_features(secrets PUBLIC cxx_std_20)

target_include_directories(secrets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
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
    )

    target_link_libraries(secrets PRIVATE ${CMAKE_DL_LIBS})
else()
    message(FATAL_ERROR
        "Unsupported operating system: ${CMAKE_SYSTEM_NAME}"
    )
endif()

option(SECRETS_BUILD_TESTS "Build secrets tests" OFF)

if(SECRETS_BUILD_TESTS)
    add_executable(secrets_credential_test
        tests/credential_test.cpp
    )
    target_link_libraries(secrets_credential_test PRIVATE secrets)
endif()
