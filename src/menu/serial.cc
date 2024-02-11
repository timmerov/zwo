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
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>

#include "serial.h"

/**
we do need to wait for a response.
10 ms is not long enough.
**/
static const int kTimeoutSeconds = 0;
static const int kTimeoutMicroseconds = 100*1000;

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

    /**
    we're going to follow the indi code.
    use the *second* implementation of connect.
    **/

    /** open the serial port. **/
    fd_ = ::open(kDevicePath, O_RDWR | O_NOCTTY);
    if (fd_ < 0) {
        LOG("Unable to open device \""<<kDevicePath<<"\"");
        return false;
    }

    /**
    the spec says you cannot set attributes cold.
    you must modify the current attributes.
    **/
    struct termios tty;
    int result = tcgetattr(fd_, &tty);
    if (result != 0) {
        LOG("Unable to get device attributes.");
        close();
        return false;
    }

    /** set baud rate for both input and output. **/
    result = cfsetspeed(&tty, kBaud);
    if (result != 0) {
        LOG("Unable to set device speed.");
        close();
        return false;
    }

    /**
    Control Modes
    set no flow control word size, parity and stop bits.
    Also don't hangup automatically and ignore modem status.
    Finally enable receiving characters.
    **/
    tty.c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD | HUPCL | CRTSCTS);
    tty.c_cflag |= (CLOCAL | CREAD);

    /** 8 bits. **/
    tty.c_cflag |= CS8;
    /** no parity. **/
    ;
    /** 1 stop bit. **/
    ;
    /** Ignore bytes with parity errors and make terminal raw and dumb. **/
    tty.c_iflag &= ~(PARMRK | ISTRIP | IGNCR | ICRNL | INLCR | IXOFF | IXON | IXANY);
    tty.c_iflag |= INPCK | IGNPAR | IGNBRK;
    /** Raw output. **/
    tty.c_oflag &= ~(OPOST | ONLCR);

    /**
    Local Modes
    Don't echo characters. Don't generate signals.
    Don't process any characters
    **/
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN | NOFLSH | TOSTOP);
    tty.c_lflag |= NOFLSH;
    /** blocking read until 1 char arrives **/
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    /** now clear input and output buffers **/
    tcflush(fd_, TCIOFLUSH);

    /** activate the new terminal settings **/
    result = tcsetattr(fd_, TCSANOW, &tty);
    if (result != 0) {
        LOG("Unable to set device attributes.");
        close();
        return false;
    }

    return true;
}

void SerialConnection::write(
    const char *cmd
) noexcept {
    if (fd_ < 0) {
        return;
    }
    int len = strlen(cmd);
    int result = ::write(fd_, cmd, len);
    (void) result;
}

std::string SerialConnection::read(
    int nbytes
) noexcept {
    std::string reply;
    if (fd_ < 0) {
        return reply;
    }

    /** let's just assume 100 bytes is enough. **/
    char buffer[100];
    int sz = sizeof(buffer)-1;
    for (int i = 0; i <= sz; ++i) {
        buffer[i] = 0;
    }

    /** special cases. **/
    bool break_on_timeout = (nbytes < 0);
    if (nbytes <= 0 || nbytes > sz) {
        nbytes = sz;
    }

    /** read bytes into the buffer. **/
    struct timeval timeout;
    auto cp = buffer;
    while (nbytes > 0) {
        /** use the select method. **/
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        timeout.tv_sec = kTimeoutSeconds;
        timeout.tv_usec = kTimeoutMicroseconds;
        int result = ::select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
        if (result == 0) {
            if (break_on_timeout) {
                break;
            }
            continue;
        }
        /** read bytes. **/
        int nread = ::read(fd_, cp, nbytes);
        if (nread <= 0) {
            break;
        }
        /** stop when we get to a '#'. **/
        for (int i = 0; i < nread; ++i) {
            if (cp[i] == '#') {
                cp[i+1] = 0;
                nread = nbytes;
                break;
            }
        }

        /** advance the buffer pointers. **/
        cp += nread;
        nbytes -= nread;
    }
    reply = buffer;

    return reply;
}

void SerialConnection::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = -1;
}
