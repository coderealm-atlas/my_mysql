#
# unit tests:
#

# add_definitions(-DTEST_ENV)
add_compile_definitions(TEST_ENV)
add_compile_definitions(TEST_BUILD)
add_compile_definitions(TEST_BUILD_GTEST)
#!!!!!!!!! it's old wide behavier. using target_include_directories instead
# include_directories(${RYML_INCLUDE_DIR})
# include catch

# find_package(unofficial-minizip CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS asio)
find_package(Boost REQUIRED COMPONENTS mysql)
find_package(Boost REQUIRED COMPONENTS uuid)
find_package(Boost REQUIRED COMPONENTS json)
find_package(Boost REQUIRED COMPONENTS iostreams)
find_package(Boost REQUIRED COMPONENTS log)
find_package(Boost REQUIRED COMPONENTS log_setup)
find_package(date CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)

# find_package(RocksDB CONFIG REQUIRED)
find_package(date CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_compile_definitions(SERVER_GIT_AGENT="git/2.34.1")
add_compile_definitions(MAX_CHUNK_LENGTH_BYTES=8)
add_compile_definitions(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS=1)
# add_compile_definitions(-DRYML_DEFAULT_CALLBACKS=0)



option(GTEST_COLOR "Enable colored output for gtest" ON)

file(GLOB LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

# ----------------------------my_mysql_test.cpp------------------------------
set(T_NAME my_mysql_test)


add_executable(${T_NAME}  
    my_mysql_test.cpp
    ${LIB_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base64.cpp
)
target_include_directories(${T_NAME}
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ./include
)

# target_precompile_headers(${T_NAME} PRIVATE ./include/pch.hpp)
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
    )
add_test(
    NAME ${T_NAME}
    COMMAND ${T_NAME}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
