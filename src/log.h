/*
Copyright (C) 2012-2021 tim cotter. All rights reserved.
*/

#pragma once

/**
log utilities and platform wrappers
that should be part of the standard libraries
but aren't.

Note: if this file is very slow to open in codeblocks...
disable symbols browser.
[Menu] Settings -> Editor... -> [Icon] Code Completion ->
    [Tab] Symbols Browser -> [Checkbox] Disable symbols browser
**/

#include <cstdarg>
#include <iostream>
#include <string>


/** handy macro for logging **/
#define LOG(...) *zwo_log::get_stream()<<zwo_log::lock \
    <<__VA_ARGS__<<std::endl \
    <<zwo_log::unlock


/** standardize logging. **/
namespace zwo_log {
    void init(const char *filename);
    void exit();
    std::ostream *get_stream();

    /** use a lock to serialize logging when multi-threaded. **/
    class Lock { public: };
    class Unlock { public: };
    extern Lock lock;
    extern Unlock unlock;

    /** log int's in hexadecimal format. **/
    class AsHex {
    public:
        AsHex(int hex);
        int value_;
    };

    /** log bytes in canonical form. **/
    void bytes(const void *bytes, int size);
}

/** insert a lock and unlock into the stream. **/
std::ostream & operator<<(std::ostream &s, const zwo_log::Lock &lock);
std::ostream & operator<<(std::ostream &s, const zwo_log::Unlock &unlock);

/** log int's in hexadecimal format. **/
std::ostream & operator<<(std::ostream &s, const zwo_log::AsHex &x);
