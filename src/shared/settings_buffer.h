/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
holds the settings for the how to process the captured image for display.
**/

#pragma once

#include <mutex>

#include <aggiornamento/aggiornamento.h>


/**
you must hold the lock before accessing any of the settings.
use this code in the code block:

    std::lock_guard<std::mutex> lock(mutex_);
**/

class Settings {
public:
    Settings() noexcept = default;
    Settings(const Settings &) = default;
    ~Settings() noexcept = default;

    bool accumulate_ = false;
    bool capture_black_ = false;
    double balance_red_ = 1.0;
    double balance_blue_ = 1.0;
    bool auto_exposure_ = false;
    int exposure_ = 100; /*microseconds*/
    bool show_focus_ = false;
    double gamma_ = 1.0;
    bool auto_iso_ = false;
    int iso_ = 100; /*no scaling*/
    bool show_histogram_ = false;
    bool show_circles_ = false;
    bool show_fps_ = false;
    std::string save_file_name_;
    std::string raw_file_name_;
};

class SettingsBuffer : public Settings {
public:
    SettingsBuffer() noexcept = default;
    SettingsBuffer(const SettingsBuffer &) = delete;
    ~SettingsBuffer() noexcept = default;

    std::mutex mutex_;

};
