cmake_minimum_required(VERSION 4.0.0)

project(
    assistantbot
    LANGUAGES CXX
    VERSION 0.1.0
)

option(ASSISTANTBOT_BUILD_TESTS "Build the test executables" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 26)

include(cmake/CPM.cmake)

find_package(Drogon CONFIG REQUIRED)
if(NOT TARGET SQLite3_lib)
    message(FATAL_ERROR "drogon is installed without SQLite support, which storage::Database needs. Run: vcpkg install 'drogon[sqlite3]'")
endif()

CPMAddPackage(
    NAME drogon-openai
    GIT_REPOSITORY https://github.com/yizhinailong/drogon-openai.git
    GIT_TAG v0.3.0
)

CPMAddPackage(
    NAME drogon-onebot
    GIT_REPOSITORY https://github.com/yizhinailong/drogon-onebot.git
    GIT_TAG v0.5.0
)

file(GLOB_RECURSE SOURCE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(REMOVE_ITEM SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

add_library(${PROJECT_NAME}-core OBJECT ${SOURCE})

target_compile_definitions(${PROJECT_NAME}-core PUBLIC ASSISTANTBOT_VERSION="${PROJECT_VERSION}")

target_include_directories(
    ${PROJECT_NAME}-core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    ${PROJECT_NAME}-core
    PUBLIC
    Drogon::Drogon
    drogon-openai::drogon-openai
    drogon-onebot::drogon-onebot
    SQLite3_lib
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME}-core PUBLIC Psapi)
endif()

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}-core)

if(ASSISTANTBOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION ${PROJECT_NAME})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist/ DESTINATION ${PROJECT_NAME}/frontend/dist)
