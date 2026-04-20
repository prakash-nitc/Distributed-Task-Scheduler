cmake_minimum_required(VERSION 3.16)

project(DistributedTaskScheduler
    VERSION 1.0.0
    DESCRIPTION "Distributed task scheduler — master/worker architecture over raw TCP"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
endif()

# pthread required by logger.hpp (std::mutex)
find_package(Threads REQUIRED)

set(HEADERS protocol.hpp logger.hpp)

add_executable(master master.cpp ${HEADERS})
target_link_libraries(master PRIVATE Threads::Threads)

add_executable(worker worker.cpp ${HEADERS})
target_link_libraries(worker PRIVATE Threads::Threads)

install(TARGETS master worker RUNTIME DESTINATION bin)
install(FILES client.py DESTINATION bin)

message(STATUS "---------------------------------------")
message(STATUS " Distributed Task Scheduler v${PROJECT_VERSION}")
message(STATUS " Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS " Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "---------------------------------------")
