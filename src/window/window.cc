/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
display images in a window.
**/

#include <ASICamera2.h>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/thread.h>

#include <shared/image_double_buffer.h>


namespace {
class WindowThread : public agm::Thread {
public:
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    ImageBuffer *img_ = nullptr;

    WindowThread(
        ImageDoubleBuffer *image_double_buffer
    ) noexcept : agm::Thread("WindowThread") {
        image_double_buffer_ = image_double_buffer;
    }

    virtual ~WindowThread() = default;

    virtual void begin() noexcept {
        LOG("WindowThread.");
        img_ = image_double_buffer_->acquire(1);
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        int wd = img_->wd_;
        LOG("WindowThread received: "<<wd);
        img_ = image_double_buffer_->swap(img_);
    }

    virtual void end() noexcept {
        LOG("WindowThread");
    }
};
}

agm::Thread *createWindowThread(
    ImageDoubleBuffer *image_double_buffer
) noexcept {
    return new(std::nothrow) WindowThread(image_double_buffer);
}
