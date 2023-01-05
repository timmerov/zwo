/*
Copyright (C) 2012-2020 tim cotter. All rights reserved.
*/

/**
hello world example.
**/

#include "log.h"


int main(
    int argc, char *argv[]
) noexcept {
    (void) argc;
    (void) argv;

    zwo_log::init("rawsome.log");

    LOG("hello world!");

    return 0;
}
