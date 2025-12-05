/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
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

class ArcSeconds {
public:
    ArcSeconds() noexcept = default;
    ~ArcSeconds() noexcept = default;

    /** initialize from ioptron string. **/
    void fromLongitude(std::string &s) noexcept;

    /** initialize from ioptron string. **/
    void fromLatitude(std::string &s) noexcept;

    /** initialize from ioptron string. **/
    void fromDeclination(std::string &s) noexcept;

    /** initialize from ioptron string. **/
    void fromRightAscension(std::string &s) noexcept;

    /** calculate degs mins secs from angle. **/
    void fromAngle() noexcept;

    /** angle.xxx +/-HH MM' SS.sss" **/
    std::string toString() noexcept;

    double angle_ = 0.0;
    int degs_ = 0;
    int mins_ = 0;
    double secs_ = 0.0;
    char east_west_ = 0;
};

enum class StarCommand {
    kNone,
    kBegin,
    kDelete,
    kDeleteAll,
    kEnd,
    kList
};

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
    double circles_x_ = 0.0;
    double circles_y_ = 0.0;
    bool show_fps_ = false;
    bool find_stars_ = false;
    StarCommand star_command_ = StarCommand::kNone;
    int star_param_ = 0;
    bool auto_save_ = false;
    bool subtract_median_ = false;
    std::string load_file_name_;
    std::string save_file_name_;
    std::string raw_file_name_;
    std::string save_path_;
    std::string input_;
    ArcSeconds right_ascension_;
    ArcSeconds declination_;
};

class SettingsBuffer : public Settings {
public:
    SettingsBuffer() noexcept = default;
    SettingsBuffer(const SettingsBuffer &) = delete;
    ~SettingsBuffer() noexcept = default;

    std::mutex mutex_;
};
