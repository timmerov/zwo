/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
display images in a window.
**/

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/master.h>
#include <aggiornamento/thread.h>

#include <shared/image_double_buffer.h>


namespace {
class WindowThread : public agm::Thread {
public:
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    ImageBuffer *img_ = nullptr;
    cv::String win_name_ = "ZWO ASI";
    bool first_image_ = false;
    cv::Mat rgb48_;
    cv::Mat gray_;
    cv::Mat laplace_;
    double base_stddev_ = 0.0;

    WindowThread(
        ImageDoubleBuffer *image_double_buffer
    ) noexcept : agm::Thread("WindowThread") {
        image_double_buffer_ = image_double_buffer;
    }

    virtual ~WindowThread() = default;

    virtual void begin() noexcept {
        LOG("WindowThread.");
        /**
        capture thread starts with buffer 0.
        window thread starts with buffer 1.
        **/
        img_ = image_double_buffer_->acquire(1);

        /** create the widnow. **/
        cv::namedWindow(win_name_);
        cv::moveWindow(win_name_, 50, 50);
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        /** we expect the first buffer to be empty. **/
        if (img_->width_ == 0) {
            LOG("WindowThread Received no image.");
            img_ = image_double_buffer_->swap(img_);
            return;
        }

        /** note once we are getting images. **/
        int wd = img_->width_;
        int ht = img_->height_;
        if (first_image_ == false) {
            first_image_ = true;
            LOG("WindowThread Received "<<wd<<"x"<<ht<<".");
        }

        /**
        convert the bayer image to rgb.
        despite the name RGB the format in memory is BGR.
        **/
        cv::cvtColor(img_->bayer_, rgb48_, cv::COLOR_BayerRG2RGB);

        /** check blurriness **/
        //checkBlurriness();

        /** show it. **/
        cv::imshow(win_name_, rgb48_);

        /** check for user input. **/
        int key = cv::waitKey(1);
        if (key >= 0) {
            /** stop all threads. **/
            LOG("WindowThread stopping all threads.");
            agm::master::setDone();
        }

        /** swap buffers with the capture thread. **/
        img_ = image_double_buffer_->swap(img_);
    }

    void checkBlurriness() noexcept {
        /**
        convert to grayscale.
        apply the laplacian convolution.
        we're basically taking the second derivative.
        calculate the standard deviation.
        which we want to maximize.
        it's weird to maximize a blurriness number.
        so print the inverse scaled arbitrarily.
        **/
        cv::cvtColor(rgb48_, gray_, cv::COLOR_RGB2GRAY);
        cv::Laplacian(gray_, laplace_, CV_64F, 3, 1, 0);
        cv::Scalar mean;
        cv::Scalar stddev;
        cv::meanStdDev(laplace_, mean, stddev);
        if (base_stddev_ == 0.0) {
            base_stddev_ = stddev[0];
        }
        auto blurriness = base_stddev_ / stddev[0];
        LOG("blurriness: "<<blurriness);
    }

    virtual void end() noexcept {
        LOG("WindowThread Closed window.");
        cv::destroyWindow(win_name_);
    }
};
}

agm::Thread *createWindowThread(
    ImageDoubleBuffer *image_double_buffer
) noexcept {
    return new(std::nothrow) WindowThread(image_double_buffer);
}
