/**
Copyright (C) 2024 tim cotter. All rights reserved.
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

        ImageBuffer img0_;
        ImageBuffer img1_;
        agm::Semaphore sem0_;
        agm::Semaphore sem1_;
    };
}

ImageBuffer::~ImageBuffer() noexcept {
    delete[] data_;
}

ImageDoubleBuffer::ImageDoubleBuffer() noexcept :
    agm::Container("ImageDoubleBuffer") {
}

ImageDoubleBuffer::~ImageDoubleBuffer() noexcept {
}

ImageDoubleBuffer *ImageDoubleBuffer::create() noexcept {
    auto impl = new(std::nothrow) ImageDoubleBufferImpl;
    return impl;
}

/** get exclusive access to one of the buffers. **/
ImageBuffer *ImageDoubleBuffer::acquire(
    int which
) noexcept {
    auto impl = (ImageDoubleBufferImpl *) this;
    if (which == 0) {
        return &impl->img0_;
    }
    if (which == 1) {
        return &impl->img1_;
    }
    return nullptr;
}

/** swap buffers with the other thread. **/
ImageBuffer *ImageDoubleBuffer::swap(
    const ImageBuffer *img
) noexcept {
    /**
    signal this buffer's semaphore.
    wait for the other buffer's semaphore.
    **/
    auto impl = (ImageDoubleBufferImpl *) this;
    if (img == &impl->img0_) {
        impl->sem0_.signal();
        impl->sem1_.waitConsume();
        return &impl->img1_;
    }
    if (img == &impl->img1_) {
        impl->sem1_.signal();
        impl->sem0_.waitConsume();
        return &impl->img0_;
    }
    return nullptr;
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
void ImageDoubleBuffer::unblock() noexcept {
    /** signal both semaphores to unblock both threads. **/
    auto impl = (ImageDoubleBufferImpl *) this;
    impl->sem0_.signal();
    impl->sem1_.signal();
}
