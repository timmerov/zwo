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
#include <shared/settings_buffer.h>


namespace {
class WindowThread : public agm::Thread {
public:
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    ImageBuffer *img_ = nullptr;
    SettingsBuffer *settings_buffer_ = nullptr;
    bool show_focus_ = false;
    cv::String win_name_ = "ZWO ASI";
    bool first_image_ = false;
    cv::Mat rgb16_;
    cv::Mat gray_;
    cv::Mat laplace_;
    cv::Mat rgb8_gamma_;
    double base_stddev_ = 0.0;
    int gamma_max_ = 0;
    agm::uint8 *gamma_ = nullptr;

    WindowThread(
        ImageDoubleBuffer *image_double_buffer,
        SettingsBuffer *settings_buffer
    ) noexcept : agm::Thread("WindowThread") {
        image_double_buffer_ = image_double_buffer;
        settings_buffer_ = settings_buffer;
    }

    virtual ~WindowThread() = default;

    virtual void begin() noexcept {
        LOG("WindowThread.");
        /**
        capture thread starts with buffer 0.
        window thread starts with buffer 1.
        **/
        img_ = image_double_buffer_->acquire(1);

        /** create the window. **/
        cv::namedWindow(win_name_);
        cv::moveWindow(win_name_, 50, 50);

        /** initialize the gamma table. **/
        initGammaTable();
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        /** we expect the first buffer to be empty. **/
        int wd = img_->width_;
        int ht = img_->height_;
        if (img_->width_ == 0) {
            LOG("WindowThread Received no image.");
            img_ = image_double_buffer_->swap(img_);
            return;
        }

        /** note once we are getting images. **/
        if (first_image_ == false) {
            first_image_ = true;
            LOG("WindowThread Received "<<wd<<"x"<<ht<<".");

            /** finish initialization now that we know the capture size. **/
            rgb8_gamma_ = std::move(cv::Mat(ht, wd, CV_8UC3));
        }

        /** copy all of the settings at once. **/
        copySettings();

        /**
        convert the bayer image to rgb.
        despite the name RGB the format in memory is BGR.
        **/
        cv::cvtColor(img_->bayer_, rgb16_, cv::COLOR_BayerRG2RGB);

        /** check blurriness. **/
        checkBlurriness();

        /** apply gamma. **/
        applyGamma();

        /** show it. **/
        cv::imshow(win_name_, rgb8_gamma_);

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

    virtual void end() noexcept {
        LOG("WindowThread Closed window.");
        cv::destroyWindow(win_name_);
    }

    void copySettings() noexcept {
        std::lock_guard<std::mutex> lock(settings_buffer_->mutex_);
        show_focus_ = settings_buffer_->show_focus_;
    }

    void checkBlurriness() noexcept {
        if (show_focus_ == false) {
            return;
        }

        /**
        convert to grayscale.
        apply the laplacian convolution.
        we're basically taking the second derivative.
        calculate the standard deviation.
        which we want to maximize.
        it's weird to maximize a blurriness number.
        so print the inverse scaled arbitrarily.
        **/
        cv::cvtColor(rgb16_, gray_, cv::COLOR_RGB2GRAY);
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

    /** the gamma table maps 16 bit to 8 bit. **/
    void initGammaTable() noexcept {
        /** most of this is divined from dcraw. **/
        /** industry standard gamma correction numbers. **/
        static const double kGamma = 2.22222;
        static const double kTs = 4.5;
        /** constants derived by complicated means from gamma and ts. **/
        static const double kPower = 1.0 / kGamma;
        double kG3 = 0.0180539;
        double kG4 = 0.0992964;
        /**
        we choose a lookup table of size 1124 because...
        it is a multiple of 4.
        and it's the smallest table that maps to all 256 byte values.
        smaller tables leave holes.
        **/
        static const int kGammaTableSize = 1124;
        static const double kGammaTableMax = kGammaTableSize - 1;
        gamma_max_ = kGammaTableSize - 1;
        gamma_ = new(std::nothrow) agm::uint8[kGammaTableSize];

        /** build the table. **/
        for (int i = 0; i < kGammaTableSize; ++i) {
            double r = double(i) / kGammaTableMax;
            double x;
            if (r < kG3) {
                /** linear at low intensities **/
                x = r * kTs;
            } else {
                /** power curve for brighter **/
                x = std::pow(r, kPower)*(1 + kG4) - kG4;
            }
            /** scale to byte sized, round, and pin. **/
            x *= 255.0;
            int ix = std::round(x);
            ix = std::max(0, std::min(ix, 255));
            /** save table value. **/
            gamma_[i] = ix;
        }
    }

    /**
    scale the source 16 bit components to the size of the gamma lookup table.
    set the destination 8 bit values.
    **/
    void applyGamma() noexcept {
        /** for each component of every pixel. **/
        static const int kChannelsPerPixel = 3;
        int wd = img_->width_;
        int ht = img_->height_;
        int sz = kChannelsPerPixel * wd * ht;
        auto src = (agm::uint16 *) rgb16_.data;
        auto dst = (agm::uint8 *) rgb8_gamma_.data;
        for (int i = 0; i < sz; ++i) {
            /** get the source component. **/
            int sval = *src++;

            /** scale to the size of the table. **/
            static const int kSourceChannelMax = 65535;
            int ix = (sval * gamma_max_ + kSourceChannelMax/2) / kSourceChannelMax;

            /** pin to 8 bits. **/
            ix = std::max(0, std::min(ix, 255));

            /** use the value from the table. **/
            *dst++ = gamma_[ix];
        }
    }
};
}

agm::Thread *createWindowThread(
    ImageDoubleBuffer *image_double_buffer,
    SettingsBuffer *settings_buffer
) noexcept {
    return new(std::nothrow) WindowThread(image_double_buffer, settings_buffer);
}
