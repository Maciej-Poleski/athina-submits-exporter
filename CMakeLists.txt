cmake_minimum_required(VERSION 3.18)
project(athina_submits_exporter)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libmagic REQUIRED IMPORTED_TARGET libmagic)


set(CMAKE_CXX_STANDARD 20)

add_executable(athina_submits_exporter main.cxx)

target_link_libraries(athina_submits_exporter PUBLIC
        PkgConfig::libmagic)
