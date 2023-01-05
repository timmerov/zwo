#
# Copyright (C) 2012-2021 tim cotter. All rights reserved.
#

# search for "you need to"

# you need to set these:
set(THIS_PROJECT_NAME zwo_project)
set(THIS_CMAKE_DIR "${CMAKE_SOURCE_DIR}/cmake")

# cmake boilerplate
# unfortunately, we can't put this in preamble.
# see preamble.txt for explanations.
set(THIS_CMAKE_VERSION 3.22.1)
cmake_minimum_required(VERSION ${THIS_CMAKE_VERSION})
message("Creating project ${THIS_PROJECT_NAME}...")
project(${THIS_PROJECT_NAME})

# more cmake boilerplate
include("${THIS_CMAKE_DIR}/scripts/preamble.txt")

# add the targets
add_subdirectory(src)
