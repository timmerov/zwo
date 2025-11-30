#
# Copyright (C) 2012-2025 tim cotter. All rights reserved.
#

# search for "you need to"

# you need to set these:
set(THIS_TARGET_NAME zwo)

# log it
message("-- Adding executable ${THIS_TARGET_NAME}...")

# gather the source files.
file(GLOB_RECURSE THIS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# gather the header files.
file(GLOB_RECURSE THIS_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# add the executable with source and includes in separate groups
add_executable(${THIS_TARGET_NAME} ${THIS_SOURCE} ${THIS_HEADERS})

# add _d to the debug target name.
set_target_properties(${THIS_TARGET_NAME} PROPERTIES DEBUG_POSTFIX _d)

# put the binary in bin.
set_target_properties(${THIS_TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# define the target in the source.
target_compile_definitions(${THIS_TARGET_NAME} PRIVATE TARGET_NAME=\"${THIS_TARGET_NAME}\")

# some directories.
set(ZWO_ASI_DIR "/home/timmer/Documents/code/astrophotography/ASI_linux_mac_SDK_V1.39")
set(AGM_DIR "${CMAKE_SOURCE_DIR}/../aggiornamento/agm")

# find the opencv package
#find_package(OpenCV REQUIRED)

# find the curses package
find_package(TIFF REQUIRED)

# add the include directories
set(INCS
#    ${OpenCV_INCLUDE_DIRS}
    "/home/timmer/Documents/code/opencv/install/include/opencv4/"
    ${ZWO_ASI_DIR}/include
    ${AGM_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}
)
include_directories(${INCS})

set(cv_installed_libs
libopencv_calib3d.so  libopencv_features2d.so  libopencv_highgui.so    libopencv_ml.so         libopencv_stitching.so
libopencv_core.so     libopencv_flann.so       libopencv_imgcodecs.so  libopencv_objdetect.so  libopencv_videoio.so
libopencv_dnn.so      libopencv_gapi.so        libopencv_imgproc.so    libopencv_photo.so      libopencv_video.so
)
link_directories("/home/timmer/Documents/code/opencv/build/lib/")
foreach(lib IN LISTS cv_installed_libs)
    list(APPEND cv_libs "/home/timmer/Documents/code/opencv/install/lib/${lib}")
endforeach()
message("=TSC= cv_libs=" ${cv_libs})

# add the libraries
set(LIBS
#    ${OpenCV_LIBS}
    ${cv_libs}
    ${TIFF_LIBRARIES}
    ${ZWO_ASI_DIR}/lib/x64/libASICamera2.so
    ${AGM_DIR}/lib/libagm.a
    readline
    history
    X11
)
target_link_libraries(${THIS_TARGET_NAME} ${LIBS})
