cmake_minimum_required(VERSION 3.26)
project(emergency_delay C)

set(CMAKE_C_STANDARD 23)

add_executable(emergency_delay main.c
        queue.c
        queue.h
        c23_compat.h)
