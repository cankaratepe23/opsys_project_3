cmake_minimum_required(VERSION 3.17)
project(opsys_project_3 C)

set(CMAKE_C_STANDARD 99)

add_executable(opsys_project_3 main.c)
find_package(Threads REQUIRED)
target_link_libraries(opsys_project_3 PRIVATE Threads::Threads)