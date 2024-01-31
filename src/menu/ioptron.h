/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
drive the ioptron smarteq pro(+) mount.
**/

#include <cstring>

#include "serial.h"


class Ioptron {
protected:
    Ioptron() noexcept;
public:
    Ioptron(const Ioptron &) = delete;
    virtual ~Ioptron() noexcept;

    static Ioptron *create() noexcept;

    void connect() noexcept;

    void disconnect() noexcept;
};
