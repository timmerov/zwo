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
    Ioptron *mount_ = nullptr;

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
        mount_->disconnect();
        delete mount_;
        mount_ = nullptr;
        LOG("MenuThread disconnected the mount.");
    }

    void parse_input() noexcept {
        if (input_.size() == 0) {
            return;
        }
        int ch = std::tolower(input_[0]);
        switch (ch) {
        case 'a':
            toggleAccumulate();
            break;

        case 'b':
            toggleCaptureBlack();
            break;

        case 'c':
            setColorBalance();
            break;

        case 'e':
            toggleAutoExposure();
            break;

        case 'f':
            toggleFocus();
            break;

        case 'g':
            toggleGamma();
            break;

        case 'h':
            toggleHistogram();
            break;

        case 'i':
            toggleIso();
            break;

        case 'm':
            handleMount();
            break;

        case 'q':
        case 27: /*escape*/
            quit();
            break;

        case 'r':
            toggleFps();
            break;

        case 's':
            saveImage();
            break;

        case 't':
            saveRaw();
            break;

        case 'x':
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
        LOG("Menu (not case sensitive unless specified):");
        LOG("  a [+-01yn]   : stack (accumulate) images: "<<settings_->accumulate_);
        LOG("  b [+-01yn]   : toggle capture black: "<<settings_->capture_black_);
        LOG("  c red blue   : set color balance: r="<<settings_->balance_red_<<" b="<<settings_->balance_blue_);
        LOG("  e [+-01yn]   : toggle auto exposure: "<<settings_->auto_exposure_);
        LOG("  e usecs      : set exposure microseconds (disables auto): "<<settings_->exposure_);
        LOG("  f [+-01yn]   : toggle manual focus helper: "<<settings_->show_focus_);
        LOG("  g pwr        : set gamma (1.0): "<<settings_->gamma_);
        LOG("  h [+-01yn]   : toggle histogram: "<<settings_->show_histogram_);
        LOG("  i [+-01yn]   : toggle auto iso linear scaling: "<<settings_->auto_iso_);
        LOG("  i iso        : set iso linear scaling [100 none] (disables auto): "<<settings_->iso_);
        LOG("  mi           : show mount info");
        LOG("  mh           : slew to home (zero) position");
        LOG("  mm [nsew] ms : slew n,s,e,w for milliseconds");
        LOG("  mr#          : set slewing rate 1-9");
        LOG("  mz           : slew to zero (home) position");
        LOG("  q,esc        : quit");
        LOG("  r [+-01yn]   : toggle fps (frame Rate): "<<settings_->show_fps_);
        LOG("  s file       : save the image (disables stacking).");
        LOG("  t file       : save the raw 16 bit image as tiff.");
        LOG("  x            : run the experiment of the day");
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

    void toggleGamma() noexcept {
        double new_gamma = getDouble(1.0);
        if (new_gamma <= 0.0) {
            new_gamma = 1.0;
        }

        LOG("MenuThread gamma: "<<new_gamma);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->gamma_ = new_gamma;
    }

    void toggleHistogram() noexcept {
        bool new_histogram = getToggleOnOff(settings_->show_histogram_);
        LOG("MenuThread histogram: "<<new_histogram);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_histogram_ = new_histogram;
    }

    void toggleIso() noexcept {
        bool new_auto_iso = false;
        int new_iso = getInt(-1);
        if (new_iso <= 0) {
            new_iso = settings_->iso_;
            new_auto_iso = getToggleOnOff(settings_->auto_iso_);
        }

        LOG("MenuThread auto iso: "<<new_auto_iso);
        LOG("MenuThread iso: "<<new_iso);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->auto_iso_ = new_auto_iso;
        settings_->iso_ = new_iso;
    }

    void toggleFps() noexcept {
        bool new_fps = getToggleOnOff(settings_->show_fps_);
        LOG("MenuThread show fps (frame rate): "<<new_fps);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_fps_ = new_fps;
    }

    void handleMount() noexcept {
        int ch = std::tolower(input_[1]);
        switch (ch) {
        case 'h':
            mount_->slewToHomePosition();
            break;

        case 'i':
            mount_->showStatus();
            break;

        case 'm': {
            int direction = input_[2];
            auto s = input_.substr(3);
            auto duration = std::stof(s);
            mount_->move(direction, duration);
        } break;

        case 'r': {
            int rate = input_[2] - '0';
            mount_->setSlewingRate(rate);
        } break;

        case 'z':
            mount_->setZeroPosition();
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

    /** parse the input as a number. **/
    double getDouble(
        double default_value
    ) noexcept {
        std::stringstream ss;
        ss << input_;
        char ch;
        double value = default_value;
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
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            std::swap(settings_->save_file_name_, filename);
        }
    }

    void saveRaw() noexcept {
        std::stringstream ss;
        ss << input_;
        char ch;
        std::string filename;
        ss >> ch >> filename;
        if (filename.size() == 0) {
            return;
        }

        LOG("MenuThread save raw: "<<filename);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            std::swap(settings_->raw_file_name_, filename);
        }
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
