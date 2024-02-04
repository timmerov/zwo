/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
run the menu thread.
**/

#include <cctype>
#include <cmath>
#include <sstream>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/nonblocking_input.h>
#include <aggiornamento/master.h>
#include <aggiornamento/thread.h>

#include <shared/settings_buffer.h>

#include "ioptron.h"


namespace {
class MenuThread : public agm::Thread {
public:
    agm::NonBlockingInput nbi_;
    SettingsBuffer *settings_ = nullptr;
    std::string input_;
    Ioptron *mount_;

    MenuThread(
        SettingsBuffer *settings_buffer
    ) noexcept : agm::Thread("MenuThread") {
        settings_ = settings_buffer;
    }

    virtual ~MenuThread() noexcept {
        delete mount_;
    }

    virtual void begin() noexcept {
        LOG("MenuThread");
        mount_ = Ioptron::create();
        mount_->connect();
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        input_ = nbi_.get();
        parse_input();
        agm::sleep::milliseconds(100);
    }

    virtual void end() noexcept {
        LOG("MenuThread");
        mount_->disconnect();
        delete mount_;
    }

    void parse_input() noexcept {
        if (input_.size() == 0) {
            return;
        }
        switch (input_[0]) {
        case 'a':
        case 'A':
            toggleAccumulate();
            break;

        case 'b':
        case 'B':
            toggleCaptureBlack();
            break;

        case 'c':
        case 'C':
            setColorBalance();
            break;

        case 'e':
        case 'E':
            toggleAutoExposure();
            break;

        case 'f':
        case 'F':
            toggleFocus();
            break;

        case 'h':
        case 'H':
            toggleHistogram();
            break;

        case 'm':
        case 'M':
            handleMount();
            break;

        case 'q':
        case 'Q':
        case 27: /*escape*/
            quit();
            break;

        case 'r':
        case 'R':
            toggleFps();
            break;

        case 's':
        case 'S':
            saveImage();
            break;

        case 'x':
        case 'X':
            experiment();
            break;

        case '?':
            showHelp();
            break;

        default:
            showMenu();
            break;
        }
    }

    void showMenu() noexcept {
        LOG("Menu:");
        LOG("  A,a [+-01yn] : stack (accumulate) images: "<<settings_->accumulate_);
        LOG("  B,b [+-01yn] : toggle capture black: "<<settings_->capture_black_);
        LOG("  C,c red blue : set color balance: r="<<settings_->balance_red_<<" b="<<settings_->balance_blue_);
        LOG("  E,e [+-01yn] : toggle auto exposure: "<<settings_->auto_exposure_);
        LOG("  E,e usecs    : set exposure microseconds: "<<settings_->exposure_<<" (disables auto exposure)");
        LOG("  F,f [+-01yn] : toggle manual focus helper: "<<settings_->show_focus_);
        LOG("  H,h [+-01yn] : toggle histogram: "<<settings_->show_histogram_);
        LOG("  Mi,mi        : show mount info");
        LOG("  Q,q,esc      : quit");
        LOG("  R,r [+-01yn] : toggle fps (frame Rate): "<<settings_->show_fps_);
        LOG("  S,s file     : save the image (disables stacking).");
        LOG("  X,x          : run the experiment of the day");
        LOG("  ?            : show help");
    }

    void toggleAccumulate() noexcept {
        bool new_accumulate = getToggleOnOff(settings_->accumulate_);
        LOG("MenuThread stack (accumulate) images: "<<new_accumulate);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->accumulate_ = new_accumulate;
    }

    void toggleCaptureBlack() noexcept {
        bool new_capture_black = getToggleOnOff(settings_->capture_black_);
        LOG("MenuThread capture black: "<<new_capture_black);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->capture_black_ = new_capture_black;
    }

    void setColorBalance() noexcept {
        std::stringstream ss;
        ss << input_;
        char ch;
        ss >> ch;
        double new_balance_red = settings_->balance_red_;
        double new_balance_blue = settings_->balance_blue_;
        ss >> new_balance_red >> new_balance_blue;
        LOG("ManeuThread color balance: r="<<new_balance_red<<" b="<<new_balance_blue);

        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->balance_red_ = new_balance_red;
        settings_->balance_blue_ = new_balance_blue;
    }

    void toggleAutoExposure() noexcept {
        bool new_auto_exposure = false;
        int new_exposure = getInt(-1);
        if (new_exposure <= 0) {
            new_exposure = settings_->exposure_;
            new_auto_exposure = getToggleOnOff(settings_->auto_exposure_);
        }

        LOG("MenuThread auto exposure: "<<new_auto_exposure);
        LOG("MenuThread exposure: "<<new_exposure);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->auto_exposure_ = new_auto_exposure;
        settings_->exposure_ = new_exposure;
    }

    void toggleFocus() noexcept {
        bool new_focus = getToggleOnOff(settings_->show_focus_);
        LOG("MenuThread focus: "<<new_focus);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_focus_ = new_focus;
    }

    void toggleHistogram() noexcept {
        bool new_histogram = getToggleOnOff(settings_->show_histogram_);
        LOG("MenuThread histogram: "<<new_histogram);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_histogram_ = new_histogram;
    }

    void toggleFps() noexcept {
        bool new_fps = getToggleOnOff(settings_->show_fps_);
        LOG("MenuThread show fps (frame rate): "<<new_fps);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_fps_ = new_fps;
    }

    void handleMount() noexcept {
        switch (input_[1]) {
        case 'i':
        case 'I':
            mount_->showStatus();
            break;
        default:
            LOG("Unknown command for mount.");
            break;
        }
    }

    /** parse the input as a number. **/
    int getInt(
        int default_value
    ) noexcept {
        std::stringstream ss;
        ss << input_;
        char ch;
        int value = default_value;
        ss >> ch >> value;
        return value;
    }

    /**
    start at the second character of the input.
    look for plus/minus 0/1 y/n.
    which means on/off.
    if none of those are found, invert the input.
    **/
    bool getToggleOnOff(
        bool cur_value
    ) noexcept {
        int ch;
        /** skip white space **/
        for (int i = 1; ; ++i) {
            ch = input_[i];
            if (ch == 0) {
                /** flip it. **/
                return !cur_value;
            }
            if (std::isspace(ch) == false) {
                break;
            }
        }
        if (ch == '-' || ch == '0' || ch == 'n') {
            /** toggle off. **/
            return false;
        }
        if (ch == '+' || ch == '1' || ch == 'y') {
            /** toggle on. **/
            return true;
        }
        /** flip it. **/
        return !cur_value;
    }

    /** stop all threads. **/
    void quit() noexcept {
        LOG("MenuThread stopping all threads.");
        agm::master::setDone();
    }

    void saveImage() noexcept {
        std::stringstream ss;
        ss << input_;
        char ch;
        std::string filename;
        ss >> ch >> filename;
        if (filename.size() == 0) {
            return;
        }

        LOG("MenuThread save file: "<<filename);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->save_file_name_ = filename;
    }

    /** run the experiment of the day. **/
    void experiment() noexcept {
        LOG("Hello, World!");
    }

    /** show how the program is to be used. **/
    void showHelp() noexcept {
        LOG("General usage:");
        LOG("-- Aim the camera at the target.");
        LOG("-- Rough focus the camera.");
        LOG("-- Wait for auto exposure to settle.");
        LOG("-- Disable auto exposure. E");
        LOG("-- Enable manual focus helper. F");
        LOG("-- Minimize the blurriness number.");
        LOG("-- Disable manual focus helper. F");
        LOG("-- Put lens cap on camera.");
        LOG("-- Enable capture black. B");
        LOG("-- Wait for black levels to settle.");
        LOG("-- Disable capture black. B");
        LOG("-- Remove lens cap from camera.");
        LOG("-- Enable histogram. H");
        LOG("-- Balance colors. C r b");
        LOG("-- Disable histogram. H");
        LOG("-- Stack (accumulate) images. A");
        LOG("-- Wait as long as you wish.");
        LOG("-- Save the image.");
        LOG("-- Profit.");
    }
};
}

agm::Thread *createMenuThread(SettingsBuffer *settings_buffer) noexcept {
    return new(std::nothrow) MenuThread(settings_buffer);
}
