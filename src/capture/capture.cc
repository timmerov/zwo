/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
capture images from the zwo asi astrophotography camera.
**/

#include <ASICamera2.h>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/thread.h>

#include <shared/image_double_buffer.h>


namespace {
class CaptureThread : public agm::Thread {
public:
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    ImageBuffer *img_ = nullptr;
    int counter_ = 0;

    CaptureThread(
        ImageDoubleBuffer *image_double_buffer
    ) noexcept : agm::Thread("CaptureThread") {
        image_double_buffer_ = image_double_buffer;
    }

    virtual ~CaptureThread() = default;

    virtual void begin() noexcept {
        LOG("CaptureThread.");
        img_ = image_double_buffer_->acquire(0);
    }

    virtual void runOnce() noexcept {
        /** count to 3 then stop all threads. **/
        ++counter_;
        if (counter_ > 3) {
            LOG("CaptureThread stopping");
            stop();
            return;
        }

        img_->wd_ = counter_;
        LOG("CaptureThread sending: "<<counter_);
        agm::sleep::milliseconds(900);
        img_ = image_double_buffer_->swap(img_);
    }

    virtual void end() noexcept {
        LOG("CaptureThread");
    }
};
}

agm::Thread *createCaptureThread(
    ImageDoubleBuffer *image_double_buffer
) noexcept {
    return new(std::nothrow) CaptureThread(image_double_buffer);
}
