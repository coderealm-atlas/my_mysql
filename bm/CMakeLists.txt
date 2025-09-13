# include google benchmark
find_package(benchmark CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

find_package(Boost REQUIRED COMPONENTS 
    asio
    mysql
    uuid
    json
    iostreams
    log
    log_setup
)
find_package(date CONFIG REQUIRED)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# Sakila MySQL Benchmark
add_executable(sakila_benchmark 
    simple_sakila_benchmark.cpp
    ../src/dummpy.cpp
)

file(GLOB HTTP_CLIENT_SOURCES "../http_client/src/*.cpp")

target_sources(sakila_benchmark PRIVATE ${HTTP_CLIENT_SOURCES})

target_include_directories(sakila_benchmark
    PRIVATE ../include
    PRIVATE ../tests/include
    PRIVATE ../http_client/include
    PRIVATE ../http_client/src
)

target_link_libraries(sakila_benchmark
    PRIVATE benchmark::benchmark
    PRIVATE benchmark::benchmark_main
    PRIVATE Boost::asio
    PRIVATE Boost::uuid
    PRIVATE Boost::json
    PRIVATE Boost::iostreams
    PRIVATE Boost::mysql
    PRIVATE date::date
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
    PRIVATE ZLIB::ZLIB
    PRIVATE Boost::log
    PRIVATE Boost::log_setup
)