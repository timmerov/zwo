/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
drive the ioptron smarteq pro(+) mount.
**/

#include <cstring>

#include <shared/settings_buffer.h>

#include "serial.h"


class Ioptron {
protected:
    Ioptron() noexcept;
public:
    Ioptron(const Ioptron &) = delete;
    virtual ~Ioptron() noexcept;

    static Ioptron *create(SettingsBuffer *settings) noexcept;

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
    void moveMilliseconds(int direction, double ms) noexcept;

    /** move the mount n,s,e,w for the specified number of milliseconds. **/
    void moveArcseconds(int direction, double ms) noexcept;

    /** start stop tracking. **/
    void setTracking(bool enabled) noexcept;

    void disconnect() noexcept;
};
