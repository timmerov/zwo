/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
drive the zwo asi astrophotography camera.

launch the threads that do the actual work.
create the containers for them to exchange data.
**/

/** vvv serial io vvv **/
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
/** ^^^ serial io ^^^ **/

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/thread.h>

#include <shared/image_double_buffer.h>
#include <shared/settings_buffer.h>


/** threads defined elsewhere. **/
extern agm::Thread *createCaptureThread(ImageDoubleBuffer *image_double_buffer, SettingsBuffer *settings_buffer);
extern agm::Thread *createWindowThread(ImageDoubleBuffer *image_double_buffer, SettingsBuffer *settings_buffer);
extern agm::Thread *createMenuThread(SettingsBuffer *settings_buffer);

/** start logging and all threads. **/
int main(
    int argc, char *argv[]
) noexcept {
    (void) argc;
    (void) argv;

    agm::log::init(TARGET_NAME ".log");

#if 0
    /** create the containers. **/
    auto image_double_buffer = ImageDoubleBuffer::create();
    SettingsBuffer settings_buffer;

    /** store the containers. **/
    std::vector<agm::Container *> containers;
    containers.push_back(image_double_buffer);

    /** create the threads. **/
    std::vector<agm::Thread *> threads;
    threads.push_back(createCaptureThread(image_double_buffer, &settings_buffer));
    threads.push_back(createWindowThread(image_double_buffer, &settings_buffer));
    threads.push_back(createMenuThread(&settings_buffer));

    /** run the threads one of them stops all of them. **/
    agm::Thread::runAll(threads, containers);
#else

    /**
    information for managing serial ports came from here:
    https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
    left a thank you very much.
    actual settings came from digging through indi driver code.
    **/

    static const auto kDevicePath = "/dev/ttyUSB0";
    static const auto kBaud = B9600;
    int fd = open(kDevicePath, O_RDWR | O_NOCTTY);
    LOG("fd="<<fd<<" fail=-1");

    struct termios tty;
    int result = tcgetattr(fd, &tty);
    LOG("tcgetattr="<<result<<" fail!=0");
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
    LOG("tcsetattr="<<result<<" fail!=0");

    result = cfsetspeed(&tty, kBaud);
    LOG("cfsetspeed="<<result<<" fail=-1");

    result = tcflush(fd, TCIFLUSH);
    LOG("tcflush(TCIFLUSH)="<<result<<" fail=-1");
    result = tcflush(fd, TCOFLUSH);
    LOG("tcflush(TCIFLUSH)="<<result<<" fail=-1");

    static const auto kVersion = ":V#";
    //static const auto kMountInfo = ":MountInfo#";
    static const auto kCmd = kVersion;
    int sz = strlen(kCmd);
    result = write(fd, kCmd, sz);
    LOG("write(\""<<kCmd<<"\","<<sz<<")="<<result<<" fail=-1");

    agm::sleep::seconds(1);

    char buffer[100];
    sz = sizeof(buffer)-1;
    for (int i = 0; i <= sz; ++i) {
        buffer[i] = 0;
    }
    struct timeval timeout;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    result = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
    LOG("select="<<result<<" success=1");
    if (result == 1) {
        int nread = read(fd, buffer, sz);
        if (0 <= nread && nread <= sz) {
            buffer[nread] = 0;
        }
        LOG("read="<<nread<<" \""<<buffer<<"\" fail<=0");
    }

    close(fd);
    LOG("close");
#endif

    return 0;
}
