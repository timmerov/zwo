/**
Copyright (C) 2012-2025 tim cotter. All rights reserved.

double buffer holds the images produced by the capture thread and displayed by the window thread.
**/

#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

#include <aggiornamento/container.h>

/** hold one image. **/
class ImageBuffer {
public:
    int width_ = 0;
    int height_ = 0;
    int bytes_ = 0;
    cv::Mat bayer_;

    ImageBuffer() noexcept = default;
    ~ImageBuffer() noexcept = default;
};

class ImageDoubleBuffer : public agm::Container {
protected:
    ImageDoubleBuffer() noexcept;
public:
    ImageDoubleBuffer(const ImageDoubleBuffer &) = delete;
    virtual ~ImageDoubleBuffer() noexcept;

    /**
    master thread creates the container.
    capture thread allocates the buffers.
    */
    static ImageDoubleBuffer *create() noexcept;

    /**
    get exclusive access to one of the buffers.
    capture thread acquires 0 first.
    window thread acquires 1 first.
    **/
    ImageBuffer *acquire(int which) noexcept;

    /**
    swap buffers with the other thread.
    this will return nullptr if it times out.
    **/
    ImageBuffer *swap(const ImageBuffer *img, int ms = 0, bool resume = false) noexcept;

    /**
    unblock both threads as if the other thread
    called swap.
    there's no way for swap to know if it was
    called normally or if it returned because
    it was unblocked.
    the caller will need to make that determination
    some other way.
    **/
    virtual void unblock() noexcept;
};
