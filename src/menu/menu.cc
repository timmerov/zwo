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


namespace {
class MenuThread : public agm::Thread {
public:
    agm::NonBlockingInput nbi_;
    SettingsBuffer *settings_ = nullptr;
    std::string input_;

    MenuThread(
        SettingsBuffer *settings_buffer
    ) noexcept : agm::Thread("MenuThread") {
        settings_ = settings_buffer;
    }
    virtual ~MenuThread() = default;

    virtual void begin() noexcept {
        LOG("MenuThread");
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        input_ = nbi_.get();
        parse_input();
        agm::sleep::milliseconds(100);
    }

    virtual void end() noexcept {
        LOG("MenuThread");
    }

    void parse_input() noexcept {
        if (input_.size() == 0) {
            return;
        }
        switch (input_[0]) {
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

        case 'q':
        case 'Q':
        case 27: /*escape*/
            quit();
            break;

        case 'x':
        case 'X':
            experiment();
            break;

        default:
            showMenu();
            break;
        }
    }

    void showMenu() noexcept {
        LOG("Menu:");
        LOG("  C,c red blue: set color balance");
        LOG("  E,e [+-01yn]: toggle auto exposure: "<<settings_->auto_exposure_);
        LOG("  E,e 123: set exposure microseconds: "<<settings_->exposure_);
        LOG("  F,f [+-01yn]: toggle manual focus helper: "<<settings_->show_focus_);
        LOG("  Q,q,esc: quit ");
        LOG("  X,x: run the experiment of the day");
    }

    void setColorBalance() noexcept {
        std::stringstream ss;
        ss << input_;
        char ch;
        ss >> ch;
        double new_balance_red = settings_->balance_red_;
        double new_balance_blue = settings_->balance_blue_;
        ss >> new_balance_red >> new_balance_blue;

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

    /** parse the input as a number. **/
    int getInt(
        int default_value
    ) noexcept {
        std::stringstream ss;
        ss << input_;
        char ch;
        ss >> ch;
        int value = default_value;
        ss >> value;
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

    /** run the experiment of the day. **/
    void experiment() noexcept {
        LOG("Hello, World!");
    }
};
}

agm::Thread *createMenuThread(SettingsBuffer *settings_buffer) noexcept {
    return new(std::nothrow) MenuThread(settings_buffer);
}
