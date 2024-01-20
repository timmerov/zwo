/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
run the menu thread.
**/

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
        case 'f':
        case 'F':
            toggleFocus();
            break;

        case 'q':
        case 'Q':
        case 27: /*escape*/
            quit();
            break;

        default:
            showMenu();
            break;
        }
    }

    void showMenu() noexcept {
        LOG("Menu:");
        LOG("  F,f: toggle manual focus helper: "<<settings_->show_focus_);
        LOG("  Q,q,esc: quit ");
    }

    void toggleFocus() noexcept {
        auto new_focus = !settings_->show_focus_;
        LOG("MenuThread focus: "<<new_focus);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_focus_ = new_focus;
    }

    void quit() noexcept {
        /** stop all threads. **/
        LOG("MenuThread stopping all threads.");
        agm::master::setDone();
    }
};
}

agm::Thread *createMenuThread(SettingsBuffer *settings_buffer) noexcept {
    return new(std::nothrow) MenuThread(settings_buffer);
}
