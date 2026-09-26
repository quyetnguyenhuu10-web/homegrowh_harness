if(NOT DEFINED HH_NPM_EXECUTABLE OR NOT DEFINED HH_TOOLS_DIR)
    message(FATAL_ERROR "npm_ci.cmake requires HH_NPM_EXECUTABLE and HH_TOOLS_DIR")
endif()

if(WIN32)
    set(HH_NPM_COMMAND cmd /d /c call "${HH_NPM_EXECUTABLE}" ci)
else()
    set(HH_NPM_COMMAND "${HH_NPM_EXECUTABLE}" ci)
endif()

execute_process(
    COMMAND ${HH_NPM_COMMAND}
    WORKING_DIRECTORY "${HH_TOOLS_DIR}"
    RESULT_VARIABLE HH_NPM_RESULT
)
if(NOT HH_NPM_RESULT EQUAL 0)
    message(FATAL_ERROR "npm ci failed: ${HH_NPM_RESULT}")
endif()
