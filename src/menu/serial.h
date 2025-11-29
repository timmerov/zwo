/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
open the usb serial port connection to the ioptron smarteq pro(+) mount.
**/

#include <cstring>


class SerialConnection {
public:
    SerialConnection() noexcept;
    SerialConnection(const SerialConnection &) = delete;
    ~SerialConnection() noexcept;

    /** returns true if successfully opened. **/
    bool open() noexcept;
    void close() noexcept;

    bool isopen() noexcept;
    void write(const char *cmd) noexcept;

    /**
    read data from the serial port.
    stops when a '#' is received.
    stops when the internal buffer is full.

    if nbytes == 0:
        will block until at least 1 character is returned.
        will not return an empty string.

    if nbytes > 0:
        stops when the specified number of bytes is received.

    if nbytes < 0:
        does not block if there is no data.
        may return an empty string.
    **/
    std::string read(int nbytes = 0) noexcept;

private:
    int fd_ = -1;
};
