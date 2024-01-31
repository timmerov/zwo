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

    /** open the serial port. **/
    fd_ = ::open(kDevicePath, O_RDWR | O_NOCTTY);
    if (fd_ < 0) {
        return false;
    }

    /**
    this is something they do in the indi code.
    seems reasonable. should we do it?

    indi/libs/indibase/connectionplugins/ttybase.cpp
    **/
#if 0
    // Note that open() follows POSIX semantics: multiple open() calls to the same file will succeed
    // unless the TIOCEXCL ioctl is issued. This will prevent additional opens except by root-owned
    // processes.
    // See tty(4) ("man 4 tty") and ioctl(2) ("man 2 ioctl") for details.

    if (ioctl(t_fd, TIOCEXCL) == -1)
    {
        DEBUGFDEVICE(m_DriverName, m_DebugChannel, "Error setting TIOCEXCL on %s - %s(%d).", device, strerror(errno), errno);
        goto error;
    }
#endif

    /**
    the spec says you cannot set attributes cold.
    you must modify the current attributes.
    **/
    struct termios tty;
    int result = tcgetattr(fd_, &tty);
    if (result != 0) {
        close();
        return false;
    }

    /**
    this is what the indi code does.
    seems to be pretty close to what mbedded.ninja was doing.
    indi/libs/indibase/connectionplugins/ttybase.cpp
    **/
    cfmakeraw(&tty);
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 10;

#if 0
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
#endif

    result = tcsetattr(fd_, TCSANOW, &tty);
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

    /**
    the indi code does this.
    indi/libs/indibase/connectionplugins/ttybase.cpp
    **/
#if 0
    // To set the modem handshake lines, use the following ioctls.
    // See tty(4) ("man 4 tty") and ioctl(2) ("man 2 ioctl") for details.

    if (ioctl(t_fd, TIOCSDTR) == -1) // Assert Data Terminal Ready (DTR)
    {
        DEBUGFDEVICE(m_DriverName, m_DebugChannel, "Error asserting DTR %s - %s(%d).", device, strerror(errno), errno);
    }

    if (ioctl(t_fd, TIOCCDTR) == -1) // Clear Data Terminal Ready (DTR)
    {
        DEBUGFDEVICE(m_DriverName, m_DebugChannel, "Error clearing DTR %s - %s(%d).", device, strerror(errno), errno);
    }

    handshake = TIOCM_DTR | TIOCM_RTS | TIOCM_CTS | TIOCM_DSR;
    if (ioctl(t_fd, TIOCMSET, &handshake) == -1)
        // Set the modem lines depending on the bits set in handshake
    {
        DEBUGFDEVICE(m_DriverName, m_DebugChannel, "Error setting handshake lines %s - %s(%d).", device, strerror(errno), errno);
    }

    // To read the state of the modem lines, use the following ioctl.
    // See tty(4) ("man 4 tty") and ioctl(2) ("man 2 ioctl") for details.

    if (ioctl(t_fd, TIOCMGET, &handshake) == -1)
        // Store the state of the modem lines in handshake
    {
        DEBUGFDEVICE(m_DriverName, m_DebugChannel, "Error getting handshake lines %s - %s(%d).", device, strerror(errno), errno);
    }
#endif

    /**
    flush any garbage a la:
    https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
    **/
    result = tcflush(fd_, TCIFLUSH);
    if (result != 0) {
        close();
        return false;
    }
    result = tcflush(fd_, TCOFLUSH);
    if (result != 0) {
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
    ::close(fd_);
    fd_ = -1;
}
