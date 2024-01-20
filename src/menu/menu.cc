/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
run the menu thread.
**/

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/nonblocking_input.h>
#include <aggiornamento/thread.h>


namespace {
class MenuThread : public agm::Thread {
public:
    agm::NonBlockingInput nbi_;

    MenuThread() noexcept : agm::Thread("MenuThread") {
    }
    virtual ~MenuThread() = default;

    virtual void begin() noexcept {
        LOG("MenuThread");
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        auto input = nbi_.get();
        if (input.size()) {
            LOG("MenuThread input: "<<input);
        }
        agm::sleep::milliseconds(100);
    }

    virtual void end() noexcept {
        LOG("MenuThread");
    }
};
}

agm::Thread *createMenuThread() noexcept {
    return new(std::nothrow) MenuThread();
}
