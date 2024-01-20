/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
run the menu thread.
**/

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/master.h>
#include <aggiornamento/thread.h>

#include <shared/image_double_buffer.h>


namespace {
class NonBlockingInput {
public:
    NonBlockingInput() = default;
    ~NonBlockingInput() = default;

    std::string input_;
    std::thread *thread_ = nullptr;
    agm::Semaphore empty_sem_;
    agm::Semaphore full_sem_;

    void start() noexcept {
        LOG("NonBlockingInput");
        thread_ = new std::thread(readInputThread, this);
        empty_sem_.signal();
    }

    std::string get() noexcept {
        std::string result;
        if (full_sem_.test()) {
            result = std::move(input_);
            full_sem_.waitConsume();
            empty_sem_.signal();
        }
        return result;
    }

    static void readInputThread(
        NonBlockingInput *nbi
    ) noexcept {
        for(;;) {
            nbi->empty_sem_.waitConsume();
            std::cin >> nbi->input_;
            nbi->full_sem_.signal();
        }
    }
};

class MenuThread : public agm::Thread {
public:
    NonBlockingInput nbi_;

    MenuThread() noexcept : agm::Thread("MenuThread") {
    }
    virtual ~MenuThread() = default;

    virtual void begin() noexcept {
        LOG("MenuThread");

        nbi_.start();
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        auto input = nbi_.get();
        if (input.size()) {
            LOG("MenuThread input: "<<input);
        }
        agm::sleep::milliseconds(300);
    }

    virtual void end() noexcept {
        LOG("MenuThread");
    }
};
}

agm::Thread *createMenuThread() noexcept {
    return new(std::nothrow) MenuThread();
}
