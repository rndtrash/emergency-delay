cmake_minimum_required(VERSION 3.25)
project(emergency_delay C)

set(CMAKE_C_STANDARD 23)

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wpedantic")
endif()

add_executable(emergency_delay main.c
        queue.c
        queue.h
        c23_compat.h)
