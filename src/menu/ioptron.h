/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
drive the ioptron smarteq pro(+) mount.
**/

#include "serial.h"


class Ioptron {
public:
    Ioptron() noexcept;
    Ioptron(const Ioptron &) = delete;
    ~Ioptron() noexcept;

    void connect() noexcept;

    void disconnect() noexcept;

private:
    SerialConnection port_;
};
