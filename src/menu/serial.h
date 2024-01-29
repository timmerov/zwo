/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
open the usb serial port connection to the ioptron smarteq pro(+) mount.
**/


class SerialConnection {
public:
    SerialConnection() noexcept;
    SerialConnection(const SerialConnection &) = delete;
    ~SerialConenction() noexcept;

    /** returns true if successfully opened. **/
    void open() noexcept;

    bool isopen() noexcept;
    void write(std::string &cmd) noexcept;
    std::string read() noexcept;
    void close() noexcept;

private:
    int fd_ = -1;
};
