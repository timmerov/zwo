/**
Copyright (C) 2024-2025 tim cotter. All rights reserved.
**/

#include <mutex>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/thread.h>

#include "image_double_buffer.h"


// use an anonymous namespace to avoid name collisions at link time.
namespace {

class ImageDoubleBufferImpl : public ImageDoubleBuffer {
public:
    ImageDoubleBufferImpl() = default;
    ImageDoubleBufferImpl(const ImageDoubleBufferImpl &) = delete;
    virtual ~ImageDoubleBufferImpl() noexcept = default;

    class SingleBuffer {
    public:
        ImageBuffer img_;
        agm::Semaphore sem_;
    };
    SingleBuffer buf0_;
    SingleBuffer buf1_;

    void init() noexcept {
        buf0_.img_.bayer_ = cv::Mat(0, 0, CV_16UC1);
        buf1_.img_.bayer_ = cv::Mat(0, 0, CV_16UC1);
    }

    /** get exclusive access to one of the buffers. **/
    ImageBuffer *acquire(
        int which
    ) noexcept {
        if (which == 0) {
            return &buf0_.img_;
        }
        if (which == 1) {
            return &buf1_.img_;
        }
        return nullptr;
    }

    /** swap buffers with the other thread. **/
    ImageBuffer *swap(
        const ImageBuffer *img,
        int ms
    ) noexcept {
        /**
        signal this buffer's semaphore.
        wait for the other buffer's semaphore.
        **/
        if (img == &buf0_.img_) {
            return swap(buf0_, buf1_, ms);
        }
        if (img == &buf1_.img_) {
            return swap(buf1_, buf0_, ms);
        }
        return nullptr;
    }

    ImageBuffer *swap(
        SingleBuffer &bufa,
        SingleBuffer &bufb,
        int ms
    ) noexcept {
        bufa.sem_.signal();
        if (ms == 0) {
            bufb.sem_.waitConsume();
            return &bufb.img_;
        }
        bool timedout = bufb.sem_.waitForConsume(ms);
        if (timedout) {
            return nullptr;
        }
        return &bufb.img_;
    }

    /**
    unblock both threads as if the other thread
    called swap.
    there's no way for swap to know if it was
    called normally or if it returned because
    it was unblocked.
    the caller will need to make that determination
    some other way.
    **/
    void unblock() noexcept {
        /** signal both semaphores to unblock both threads. **/
        buf0_.sem_.signal();
        buf1_.sem_.signal();
    }
};
}

ImageDoubleBuffer::ImageDoubleBuffer() noexcept :
    agm::Container("ImageDoubleBuffer") {
}

ImageDoubleBuffer::~ImageDoubleBuffer() noexcept {
}

ImageDoubleBuffer *ImageDoubleBuffer::create() noexcept {
    auto impl = new(std::nothrow) ImageDoubleBufferImpl;
    impl->init();
    return impl;
}

ImageBuffer *ImageDoubleBuffer::acquire(
    int which
) noexcept {
    auto impl = (ImageDoubleBufferImpl *) this;
    return impl->acquire(which);
}

ImageBuffer *ImageDoubleBuffer::swap(
    const ImageBuffer *img,
    int ms
) noexcept {
    auto impl = (ImageDoubleBufferImpl *) this;
    return impl->swap(img, ms);
}

void ImageDoubleBuffer::unblock() noexcept {
    /** signal both semaphores to unblock both threads. **/
    auto impl = (ImageDoubleBufferImpl *) this;
    impl->unblock();
}
