# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2023 Amol Surati

cmake_minimum_required(VERSION 3.25)
project(c14vm VERSION 0.0.1)

set(GENERATOR_COMMAND make)
set(C_STANDARD 17)
set(C_STANDARD_REQUIRED ON)
set(C_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR})
add_compile_options(-std=c17 -Wall -Wextra -Werror -Wshadow -Wpedantic)
add_compile_options(-Wfatal-errors -pedantic-errors)

# lld when compiling with clang
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	add_link_options(-fuse-ld=lld)
endif()

add_executable(c14vm
	src/main.c
	src/parser.c
	src/scanner.c
)

#valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose
#--log-file=v.txt --num-callers=100
#-vgdb-error=0
