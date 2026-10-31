file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
)

foreach(TEST_SOURCE IN LISTS TEST_SOURCES)
    cmake_path(GET TEST_SOURCE STEM TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE ${PROJECT_NAME}-core)

    # Tests load tests/config.json from the repo root, never the app's
    # config.json there. The app-backed ones share its listener port, so the
    # lock keeps them apart under ctest -j.
    add_test(
        NAME ${TEST_NAME}
        COMMAND ${TEST_NAME}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
    set_tests_properties(
        ${TEST_NAME}
        PROPERTIES
        RESOURCE_LOCK test_config_listener
        TIMEOUT 60
    )
endforeach()
