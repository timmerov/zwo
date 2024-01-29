/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
open the usb serial port connection to the ioptron smarteq pro(+) mount.

information for managing serial ports came from here:
https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
left a thank you very much.
actual settings came from digging through indi driver code.
**/

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>

class SerialConnection {
public:
    SerialConnection() noexcept;
    SerialConnection(const SerialConnection &) = delete;
    ~SerialConenction() noexcept;

    bool open() noexcept;
    bool isopen() noexcept;
    void write(std::string &cmd);
    std::string read();
    void close() noexcept;

private:
    int fd = -1;
};

SerialConnection::SerialConnection() noexcept {
}

SerialConnection::~SerialConnection() noexcept {
    close();
}

bool SerialConnection::open() noexcept {
    /** assume the device path is always the same. **/
    static const auto kDevicePath = "/dev/ttyUSB0";

    /** docs say 9600 baud, 8 bits, no parity, 1 stop bit. **/
    static const auto kBaud = B9600;

    /** open the serial port. **/
    int fd = open(kDevicePath, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return false;
    }
    LOG("fd="<<fd<<" fail=-1");

    /**
    the spec says you cannot set attributes cold.
    you must modify the current attributes.
    **/
    struct termios tty;
    int result = tcgetattr(fd, &tty);
    if (result != 0) {
        close();
        return false;
    }

    /**
    again, most of this information came from here:
    https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
    **/
    /** no parity **/
    tty.c_cflag &= ~PARENB;
    /** one stop bit **/
    tty.c_cflag &= ~CSTOPB;
    /** 8 bits **/
    tty.c_cflag |= CS8;
    /** disable hardware control cause no extra wires. **/
    tty.c_cflag &= ~CRTSCTS;
    /** disable modem specific signals. **/
    tty.c_cflag |= CLOCAL;
    /** enable reading. **/
    tty.c_cflag |= CREAD;
    /** do not wait for line feeds. **/
    tty.c_lflag &= ~ICANON;
    /** disable echo, erasure, and new lines **/
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    /** disable signals **/
    tty.c_lflag &= ~ISIG;
    /** disable software flow control. **/
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    /** disable special handling of input. **/
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);
    /** disable special handling of output. **/
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;
    /** wait 1s (10 deciseconds) for data. **/
    tty.c_cc[VTIME] = 10;
    /** read a minimum of 0 bytes. **/
    tty.c_cc[VMIN] = 0;
    result = tcsetattr(fd, TCSANOW, &tty);
    if (result != 0) {
        close();
        return false;
    }

    /** set baud rate for both input and output. **/
    result = cfsetspeed(&tty, kBaud);
    if (result != 0) {
        close();
        return false;
    }

    /** flush any garbage. **/
    result = tcflush(fd, TCIFLUSH);
    if (result != 0) {
        close();
        return false;
    }
    result = tcflush(fd, TCOFLUSH);
    if (result != 0) {
        close();
        return false;
    }

    return true;
}

void SerialConnection::write(
    std::string *cmd
) noexcept {
    int len = cmd.length();
    int result = write(fd_, cmd, len);
}

std::string SerialConnection::read() noexcept {
    std::string reply;

    /** let's just assume 100 bytes is enough. **/
    char buffer[100];
    sz = sizeof(buffer)-1;
    for (int i = 0; i <= sz; ++i) {
        buffer[i] = 0;
    }

    /** read bytes until we get a # **/
    bool end_of_input = false;
    auto cp = &buffer[0];
    for(;;) {
        /** use the select method. **/
        struct timeval timeout;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int result = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
        if (result != 1) {
            break;
        }
        int nread = read(fd_, cp, sz);
        if (nread > 0) {
            for (int i = 0; i < nread; ++i) {
                if (cp[i] == '#') {
                    cp[i+1] = 0;
                    end_of_input = true;
                    break;
                }
            }
            if (end_of_input) {
                reply = buffer;
                break;
            }
        }

        return reply;
    }
}

void SerialConnection::close() noexcept {
    close(fd_);
    fd_ = -1;
}
