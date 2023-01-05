/*
Copyright (C) 2012-2021 tim cotter. All rights reserved.
*/

/**
implementation of utilities and platform abstractions.
**/

#include "log.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>


zwo_log::Lock zwo_log::lock;
zwo_log::Unlock zwo_log::unlock;

namespace {
class LogStreams {
public:
    std::ofstream file_;
    std::stringstream str_;
};

LogStreams *get_log_streams() {
    static LogStreams g_log_streams;
    return &g_log_streams;
}

std::mutex *get_log_mutex() {
    static std::mutex g_mutex;
    return &g_mutex;
}

void log_line_of_bytes(
    int index,
    const char *ptr,
    int count
) {
    std::string s;
    s.reserve(4+1+32*3+32);
    static const char hexdigits[] = "0123456789ABCDEF";
    auto ch1 = hexdigits[(index>>12)&0xf];
    auto ch2 = hexdigits[(index>>8)&0xf];
    auto ch3 = hexdigits[(index>>4)&0xf];
    auto ch4 = hexdigits[index&0xf];
    s += ch1;
    s += ch2;
    s += ch3;
    s += ch4;
    s += ' ';
    for (auto i = 0; i < count; ++i) {
        unsigned char x = ptr[i];
        auto ch5 = hexdigits[x>>4];
        auto ch6 = hexdigits[x&0xf];
        s += ch5;
        s += ch6;
        s += ' ';
    }
    for (auto i = 0; i < count; ++i) {
        char ch7 = ptr[i];
        unsigned char uch = ch7; // wtf?
        if (std::isprint(uch) == false) {
            ch7 = '.';
        }
        s += ch7;
    }
    LOG(s.c_str());
}
} // anonymous namespace

void zwo_log::init(
  const char *filename
) {
    auto ls = get_log_streams();
    if (ls->file_.is_open() == false) {
        ls->file_.open(filename, std::ios::out | std::ios::trunc);
    }
}

void zwo_log::exit() {
    auto ls = get_log_streams();
    ls->file_.close();
}

std::ostream *zwo_log::get_stream() {
    auto ls = get_log_streams();
    return &ls->str_;
}

zwo_log::AsHex::AsHex(
    int hex
) :
    value_(hex) {
}

void zwo_log::bytes(
    const void *vp,
    int size
) {
    auto ptr = (const char *) vp;
    for (auto i = 0; size > 0; i += 24) {
        auto n = std::min(size, 24);
        log_line_of_bytes(i, ptr, n);
        size -= n;
        ptr  += n;
    }
}

std::ostream & operator<<(
    std::ostream &s,
    const zwo_log::Lock &lock
) {
    (void) lock;
    auto m = get_log_mutex();
    m->lock();
    return s;
}

std::ostream & operator<<(
    std::ostream &s,
    const zwo_log::Unlock &unlock
) {
    (void) unlock;
    auto ls = get_log_streams();
    // write to the file.
    ls->file_ << ls->str_.str();
    // write to the console.
#if defined(AGM_WINDOWS)
    OutputDebugStringA(ls->str_.str().c_str());
#endif
    std::cout << ls->str_.str();
    // clear the string.
    ls->str_.str(std::string());
    auto m = get_log_mutex();
    m->unlock();
    return s;
}

std::ostream & operator<<(
    std::ostream &s,
    const zwo_log::AsHex &hex
) {
    s << "0x" << std::hex << hex.value_ << std::dec;
    return s;
}
