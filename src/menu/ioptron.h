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

    bool connect() noexcept;

    bool isConnected() noexcept;

    /** dump a bunch of mount status. **/
    void showStatus() noexcept;

    /** slew to the currently set home/zero position. **/
    void slewToHomePosition() noexcept;

    /** set the zero/home position to the current position of the mount. **/
    void setZeroPosition() noexcept;

    /** rate is 1 to 9 **/
    void setSlewingRate(int rate) noexcept;

    /** move the mount n,s,e,w for the specified number of milliseconds. **/
    void move(int direction, float duration) noexcept;

    void disconnect() noexcept;
};
