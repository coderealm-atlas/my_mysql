#
# unit tests:
#

add_compile_definitions(TEST_ENV)
add_compile_definitions(TEST_BUILD)
add_compile_definitions(TEST_BUILD_GTEST)
#!!!!!!!!! it's old wide behavier. using target_include_directories instead
# include_directories(${RYML_INCLUDE_DIR})
# include catch

# find_package(unofficial-minizip CONFIG REQUIRED)
# Find packages in correct order - OpenSSL first
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(date CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# Boost components
find_package(Boost REQUIRED COMPONENTS 
    asio
    mysql
    uuid
    json
    iostreams
    log
    log_setup
)

add_compile_definitions(SERVER_GIT_AGENT="git/2.34.1")
add_compile_definitions(MAX_CHUNK_LENGTH_BYTES=8)
add_compile_definitions(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS=1)



option(GTEST_COLOR "Enable colored output for gtest" ON)

file(GLOB LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

# ----------------------------my_mysql_test.cpp------------------------------
set(T_NAME my_mysql_test)

# Include http_client source files
file(GLOB HTTP_CLIENT_SOURCES ${CMAKE_SOURCE_DIR}/http_client/src/*.cpp)

add_executable(${T_NAME}  
    my_mysql_test.cpp
    ${LIB_SOURCES}
    ${HTTP_CLIENT_SOURCES}
)
target_include_directories(${T_NAME}
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/http_client/include # prefer submodule headers
    PRIVATE ${CMAKE_SOURCE_DIR}/http_client/src
    PRIVATE ./include # only test-specific helpers (common_macros.hpp, tutil.hpp, pch.hpp, client_pool_ssl.hpp)
)

target_link_libraries(
    ${T_NAME}
    PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
    PUBLIC 
        Boost::asio
        Boost::uuid
        Boost::json
        Boost::iostreams
        Boost::mysql
        date::date
        OpenSSL::SSL
        OpenSSL::Crypto
        ZLIB::ZLIB
        Boost::log
        Boost::log_setup
        fmt::fmt-header-only
        ryml::ryml
    )
add_test(
    NAME ${T_NAME}
    COMMAND ${T_NAME}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ----------------------------sakila_integration_test------------------------------
# Sakila Integration Tests
set(SAKILA_T_NAME sakila_integration_test)

file(GLOB HTTP_CLIENT_SOURCES
    "../http_client/src/*.cpp"
)

add_executable(${SAKILA_T_NAME}
    sakila_integration_test.cpp
    ../src/dummpy.cpp
    ${HTTP_CLIENT_SOURCES}
)
target_include_directories(${SAKILA_T_NAME}
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/http_client/include # prefer submodule headers
    PRIVATE ${CMAKE_SOURCE_DIR}/http_client/src
    PRIVATE ./include # only test-specific helpers (common_macros.hpp, tutil.hpp, pch.hpp, client_pool_ssl.hpp)
)

target_link_libraries(
    ${SAKILA_T_NAME}
    PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
    PUBLIC 
        Boost::asio
        Boost::uuid
        Boost::json
        Boost::iostreams
        Boost::mysql
        date::date
        OpenSSL::SSL
        OpenSSL::Crypto
        ZLIB::ZLIB
        Boost::log
        Boost::log_setup
        fmt::fmt-header-only
        ryml::ryml
    )
add_test(
    NAME ${SAKILA_T_NAME}
    COMMAND ${SAKILA_T_NAME}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
