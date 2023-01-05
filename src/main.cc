/*
Copyright (C) 2012-2020 tim cotter. All rights reserved.
*/

/**
hello world example.
**/

#include <ASICamera2.h>

#include "log.h"


int main(
    int argc, char *argv[]
) noexcept {
    (void) argc;
    (void) argv;

    zwo_log::init("rawsome.log");

    LOG("hello, world!");

    int num_cameras = ASIGetNumOfConnectedCameras();
    LOG("num_cameras="<<num_cameras);

    LOG("goodbye, world!");

    return 0;
}
