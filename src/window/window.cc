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
    /** share data with the capture thread. **/
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    ImageBuffer *img_ = nullptr;
    /** share data with the menu thread. **/
    SettingsBuffer *settings_buffer_ = nullptr;
    bool capture_black_ = 0;
    double balance_red_ = 1.0;
    double balance_blue_ = 1.0;
    bool show_focus_ = false;
    bool show_histogram_ = false;
    std::string save_file_name_;

    /** our fields. **/
    cv::String win_name_ = "ZWO ASI";
    bool first_image_ = false;
    cv::Mat rgb16_;
    cv::Mat cropped_;
    cv::Mat gray_;
    cv::Mat laplace_;
    cv::Mat rgb8_gamma_;
    double base_stddev_ = 0.0;
    int gamma_max_ = 0;
    agm::uint8 *gamma_ = nullptr;
    int *histr_ = nullptr;
    int *histg_ = nullptr;
    int *histb_ = nullptr;
    agm::uint16 blackr_ = 0;
    agm::uint16 blackg_ = 0;
    agm::uint16 blackb_ = 0;

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

        /** capture black. **/
        captureBlack();

        /** suptract black. **/
        subtractBlack();

        /** adjust BGR colors. **/
        convertStdRgb();

        /** balance colors. **/
        balanceColors();

        /** show histogram. **/
        showHistogram();

        /** apply gamma. **/
        applyGamma();

        /** show it. **/
        cv::imshow(win_name_, rgb8_gamma_);

        /** save the file. **/
        saveImage();

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
        capture_black_ = settings_buffer_->capture_black_;
        balance_red_ = settings_buffer_->balance_red_;
        balance_blue_ = settings_buffer_->balance_blue_;
        show_focus_ = settings_buffer_->show_focus_;
        show_histogram_ = settings_buffer_->show_histogram_;
        save_file_name_ = std::move(settings_buffer_->save_file_name_);
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
        int wd = img_->width_;
        int ht = img_->height_;
        auto wd_range = cv::Range(wd/4, wd*3/4);
        auto ht_range = cv::Range(ht/4, ht*3/4);
        cropped_ = rgb16_(ht_range, wd_range);
        cv::cvtColor(cropped_, gray_, cv::COLOR_RGB2GRAY);
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

    void captureBlack() noexcept {
        if (capture_black_ == false) {
            return;
        }

        auto mean = cv::mean(rgb16_);
        int r = blackr_ * 95 / 100;
        int g = blackg_ * 95 / 100;
        int b = blackb_ * 95 / 100;
        blackr_ = (r + mean[2]) / 2;
        blackg_ = (g + mean[1]) / 2;
        blackb_ = (b + mean[0]) / 2;
        LOG("new black: r="<<blackr_<<" g="<<blackg_<<" b="<<blackb_);
    }

    /**
    subtract black from the image.
    pin to 0.
    **/
    void subtractBlack() noexcept {
        int sz = img_->width_ * img_->height_;
        auto ptr = (agm::uint16 *) rgb16_.data;
        for (int i = 0; i < sz; ++i) {
            int r = ptr[2];
            int g = ptr[1];
            int b = ptr[0];
            r -= blackr_;
            g -= blackg_;
            b -= blackb_;
            r = std::max(0, r);
            g = std::max(0, g);
            b = std::max(0, b);
            ptr[2] = r;
            ptr[1] = g;
            ptr[0] = b;
            ptr += 3;
        }
    }

    void convertStdRgb() noexcept {
        /** adjust BGR colors **/
        int sz = img_->width_ * img_->height_;
        auto ptr = (agm::uint16 *) rgb16_.data;
        for (int i = 0; i < sz; ++i) {
            int r0 = ptr[2];
            int g0 = ptr[1];
            int b0 = ptr[0];
            /**
            convert observed rgb to srgb using best guess matrix.
            the camera was shown black, red, green, blue screens.
            a response matrix was constructed.
            scaling r,g,b was a bit non-intuitive.
            the matrix was inverted to get this color correction matrix.
            which is good enough for now.
            we still have too much red and blue for color balancing.
            but i guess that's okay.
            **/
            int r1 = ( 57*r0 -   9*g0 -   0*b0)/100;
            int g1 = (-35*r0 + 100*g0 -  13*b0)/100;
            int b1 = (  2*r0 -  27*g0 +  68*b0)/100;
            r1 = std::max(std::min(r1, 65535),0);
            g1 = std::max(std::min(g1, 65535),0);
            b1 = std::max(std::min(b1, 65535),0);
            ptr[2] = r1;
            ptr[1] = g1;
            ptr[0] = b1;
            ptr += 3;
        }
    }

    void balanceColors() noexcept {
        int sz = img_->width_ * img_->height_;
        auto ptr = (agm::uint16 *) rgb16_.data;
        for (int i = 0; i < sz; ++i) {
            int r = ptr[2];
            int b = ptr[0];
            r = std::round(r * balance_red_);
            b = std::round(b * balance_blue_);
            ptr[2] = r;
            ptr[0] = b;
            ptr += 3;
        }
    }

    void showHistogram() noexcept {
        if (show_histogram_ == false) {
            return;
        }

        int wd = img_->width_;
        int ht = img_->height_;
        int hist_sz = wd + 1;
        if (histr_ == nullptr) {
            histr_ = new(std::nothrow) int[hist_sz];
            histg_ = new(std::nothrow) int[hist_sz];
            histb_ = new(std::nothrow) int[hist_sz];
            for (int i = 0; i < hist_sz; ++i) {
                histr_[i] = 0;
                histg_[i] = 0;
                histb_[i] = 0;
            }
        }
        for (int i = 0; i < hist_sz; ++i) {
            histr_[i] = histr_[i] * 95 / 100;
            histg_[i] = histg_[i] * 95 / 100;
            histb_[i] = histb_[i] * 95 / 100;
        }
        int sz = wd * ht;
        auto ptr = (agm::uint16 *) rgb16_.data;
        for (int i = 0; i < sz; ++i) {
            int r = ptr[2];
            int g = ptr[1];
            int b = ptr[0];
            ptr += 3;
            r = r * wd / 65536;
            g = g * wd / 65536;
            b = b * wd / 65536;
            r = std::max(0, std::min(r, wd));
            g = std::max(0, std::min(g, wd));
            b = std::max(0, std::min(b, wd));
            ++histr_[r];
            ++histg_[g];
            ++histb_[b];
        }

        plotHistogram(histr_, 2);
        plotHistogram(histg_, 1);
        plotHistogram(histb_, 0);
    }

    void plotHistogram(
        int *hist,
        int color
    ) noexcept {
    #if 0
        /**
        plotting the histogram as a log has potential.
        the max value of the histogram would be about 20*wd*ht.
        if everything were evenly distributed it would be about 20*ht.
        we want 20*ht to be about half way up the screen.
        but that puts tiny values way too high.
        i suppose we could look this up in a table for speed.
        but it might be kinda big.
        **/
        (void) hist;
        (void) color;
        int wd = img_->width_;
        int ht = img_->height_;
        int htm1 = ht - 1;
        auto ptr = (agm::uint16 *) rgb16_.data;

        for (int x = 0; x < wd; ++x) {
            int y = std::round(double(ht) * std::pow(double(x)/double(wd), k));
            y = htm1 - y;
            y = std::max(0, std::min(y, htm1));
            LOG("x="<<x<<" y="<<y);
            auto dst = &ptr[3*wd*y];
            for (int i = 0; i < 3; ++i) {
                dst[i] = 65535;
            }
            ptr += 3;
        }
    #endif

        int wd = img_->width_;
        int ht = img_->height_;
        int htm1 = ht - 1;
        double mx = double(20 * wd * ht);
        double k = std::log(2.0) / std::log(double(wd));
        auto ptr = (agm::uint16 *) rgb16_.data;
        for (int x = 0; x < wd; ++x) {
            int c0 = hist[x+0];
            int c1 = hist[x+1];
            c0 = std::round(double(ht) * std::pow(double(c0)/mx, k));
            c1 = std::round(double(ht) * std::pow(double(c1)/mx, k));
            //c0 = c0 / 400;
            //c1 = c1 / 400;
            c0 = std::max(0, std::min(c0, htm1));
            c1 = std::max(0, std::min(c1, htm1));
            c0 = htm1 - c0;
            c1 = htm1 - c1;
            if (c0 > c1) {
                std::swap(c0, c1);
            }
            for (int y = c0; y <= c1; ++y) {
                auto dst = &ptr[3*wd*y];
                dst[color] = 65535;
            }
            ptr += 3;
        }
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

    /** save the image to the file. **/
    void saveImage() noexcept {
        if (save_file_name_.size() == 0) {
            return;
        }

        /** this is why you do not throw exceptions ever. **/
        try {
            cv::imwrite(save_file_name_, rgb8_gamma_);
            LOG("CaptureThread Saved image to file: "<<save_file_name_);
        } catch (const cv::Exception& ex) {
            LOG("CaptureThread Failed to save image to file: "<<save_file_name_<<" OpenCV reason: "<<ex.what());
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
