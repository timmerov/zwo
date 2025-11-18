/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
display images in a window.
**/

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <tiffio.h>
#include <X11/Xlib.h>

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
    bool accumulate_ = false;
    bool capture_black_ = false;
    double balance_red_ = 1.0;
    double balance_blue_ = 1.0;
    int exposure_ = 100;
    bool show_focus_ = false;
    double gamma_ = 1.0;
    bool auto_iso_ = false;
    int iso_ = 100;
    bool show_circles_ = false;
    double circles_x_ = 0.0;
    double circles_y_ = 0.0;
    bool show_histogram_ = false;
    bool show_fps_ = false;
    std::string save_file_name_;
    std::string raw_file_name_;

    /** our fields. **/
    cv::String win_name_ = "ZWO ASI";
    bool first_image_ = false;
    cv::Mat rgb16_;
    cv::Mat black_;
    cv::Mat rgb32_;
    cv::Mat cropped_;
    cv::Mat gray_;
    cv::Mat laplace_;
    cv::Mat rgb8_gamma_;
    double base_stddev_ = 0.0;
    int gamma_max_ = 0;
    agm::uint8 *gamma_table_ = nullptr;
    int *histr_ = nullptr;
    int *histg_ = nullptr;
    int *histb_ = nullptr;
    int nstacked_ = 0;
    agm::int64 fps_start_ = 0;
    int fps_count_ = 0;
    int display_width_ = 0;
    int display_height_ = 0;
    cv::Rect aoi_;

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

        /** limit window size to display size. **/
        getDisplayResolution();
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
            rgb8_gamma_ = cv::Mat(ht, wd, CV_8UC3);

            /** set the area of interest. **/
            setWindowCrop();
        }

        /** display fps every 3 seconds. **/
        if (show_fps_) {
            if (fps_start_ == 0) {
                fps_start_ = agm::time::microseconds();
            }
            ++fps_count_;
            auto now = agm::time::microseconds();
            auto elapsed = now - fps_start_;
            if (elapsed > 3000000LL) {
                double fps = double(fps_count_) * 1000000.0 / double(elapsed);
                LOG("WindowThread fps: "<<fps);
                fps_count_ = 0;
                fps_start_ = 0;
            }
        } else {
            fps_start_ = 0;
            fps_count_ = 0;
        }

        /** copy all of the settings at once. **/
        copySettings();

        /** capture black. **/
        captureBlack();

        /** subtract black. **/
        subtractBlack();

        /**
        convert the bayer image to rgb.
        despite the name RGB the format in memory is BGR.
        **/
        cv::cvtColor(img_->bayer_, rgb16_, cv::COLOR_BayerRG2RGB);

        /** check blurriness. **/
        checkBlurriness();

        /** stack images. **/
        stackImages();

        /** iso linear scale. **/
        isoLinearScale();

        /** gamma power scale. **/
        gammaPowerScale();

        /** adjust BGR colors. **/
        convertStdRgb();

        /** balance colors. **/
        balanceColors();

        /** show histogram. **/
        showHistogram();

        /** show collimation circlex. **/
        showCollimationCircles();

        /** apply display gamma. **/
        applyDisplayGamma();

        /** crop it. **/
        cv::Mat cropped = rgb8_gamma_(aoi_);

        /** show it. **/
        cv::imshow(win_name_, cropped);

        /** save the file. **/
        saveImage();

        /** check for user hits escape key. **/
        wait_for_swap();
    }

    /**
    we need o call cv::waitKey periodically.
    we also need to wait for a new captured image.
    which could take a long time.
    long enough that the os thinks the program crashed.
    */
    void wait_for_swap() noexcept {
        static const int kTimeoutMs = 100;

        for(;;) {
            /** stop waiting if we're quitting. **/
            if (isRunning() == false) {
                return;
            }

            /** do the window things. **/
            int key = cv::waitKey(1);

            /**
            user hit escape key.
            stop all threads.
            **/
            if (key == 27) {
                LOG("WindowThread stopping all threads.");
                agm::master::setDone();
                return;
            }

            /** swap buffers with the capture thread. **/
            auto img = image_double_buffer_->swap(img_, kTimeoutMs);

            /** we have a new image. **/
            if (img) {
                img_ = img;
                return;
            }

            /** no new image. loop. **/
        }
    }

    virtual void end() noexcept {
        cv::destroyWindow(win_name_);
        LOG("WindowThread Closed window.");
    }

    void copySettings() noexcept {
        std::lock_guard<std::mutex> lock(settings_buffer_->mutex_);
        accumulate_ = settings_buffer_->accumulate_;
        capture_black_ = settings_buffer_->capture_black_;
        balance_red_ = settings_buffer_->balance_red_;
        balance_blue_ = settings_buffer_->balance_blue_;
        exposure_ = settings_buffer_->exposure_;
        show_focus_ = settings_buffer_->show_focus_;
        gamma_ = settings_buffer_->gamma_;
        show_histogram_ = settings_buffer_->show_histogram_;
        auto_iso_ = settings_buffer_->auto_iso_;
        iso_ = settings_buffer_->iso_;
        show_circles_ = settings_buffer_->show_circles_;
        circles_x_ = settings_buffer_->circles_x_;
        circles_y_ = settings_buffer_->circles_y_;
        show_fps_ = settings_buffer_->show_fps_;
        save_file_name_ = std::move(settings_buffer_->save_file_name_);
        raw_file_name_ = std::move(settings_buffer_->raw_file_name_);
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

        /** the first black image. **/
        int wd = img_->width_;
        int ht = img_->height_;
        if (black_.rows == 0) {
            black_ = cv::Mat(ht, wd, CV_16UC1);
            black_ = 0;
        }

        /**
        we're looking for pixels that leak too much current.
        in other words, they're too bright.
        some are way too bright.
        they get brighter linearly with exposure time.
        save the brightest values.
        scale to an exposure time of 1 second = 10000000 us.
        leakage apparently varies with temperature.
        which is unfortunate.
        and is why we save the maximum value.
        **/
        int mx = 0;
        int sz = wd * ht;
        auto pimg = (agm::uint16 *) img_->bayer_.data;
        auto pblk = (agm::uint16 *) black_.data;
        for (int i = 0; i < sz; ++i) {
            /** 16 bits **/
            int pix = *pimg;
            agm::int64 x = *pblk;
            /** +20 bits = 36 bits > 32 bits. **/
            x = x * 1000000 / exposure_;
            int blk = std::min(x, 65535L);
            blk = std::max(blk, pix);
            mx = std::max(mx, blk);
            *pblk++ = blk;

            /** show black while we're capturing it. **/
            *pimg++ = blk;
        }
        LOG("max black leakage per second: "<<mx);
    }

    /**
    subtract black from the image.
    **/
    void subtractBlack() noexcept {
        /** no black to subtract. **/
        if (black_.rows == 0) {
            return;
        }
        /** don't subtract black if we're capturing black. **/
        if (capture_black_) {
            return;
        }

        /**
        scale black to exposure time.
        subtract from captured image.
        **/
        int mx = 0;
        int sz = img_->width_ * img_->height_;
        auto pimg = (agm::uint16 *) img_->bayer_.data;
        auto pblk = (agm::uint16 *) black_.data;
        for (int i = 0; i < sz; ++i) {
            /** 16 bits **/
            int pix = *pimg;
            agm::int64 x = *pblk++;
            /** +28 bits = 44 bits > 32 bits **/
            x = x * exposure_ / 1000000;
            int blk = std::min(x, 65535L);
            pix -= blk;
            pix = std::max(0, pix);
            mx = std::max(mx, pix);

            /**
            there's a bit of an issue here.
            some pixels are very leaky.
            and quite variable in their leakiness.
            so for these suspect pixels with high black values...
            we don't know what the correct black value is now.
            so we should handle suspect pixels by comparing the
            correct value to the values of its neighbors.
            if it's out of whack, then we should use the average
            value of its neighbors instead of the corrected value.
            **/
            *pimg++ = pix;
        }
        LOG("max captured component: "<<mx);
    }

    /** no alignment **/
    void stackImages() noexcept {
        if (accumulate_ == false) {
            return;
        }

        int wd = img_->width_;
        int ht = img_->height_;
        int sz = 3 * wd * ht;

        /** the first image. **/
        if (rgb32_.rows == 0) {
            rgb32_ = cv::Mat(ht, wd, CV_32SC3);
            rgb32_ = 0;
        }

        /**
        accumulate the 16 bit values into the 32 bit sums.
        save the maximum value.
        **/
        int mx = 0;
        auto ptr16 = (agm::uint16 *) rgb16_.data;
        auto ptr32 = (agm::int32 *) rgb32_.data;
        for (int i = 0; i < sz; ++i) {
            agm::int32 px = *ptr32;
            px += *ptr16++;
            mx = std::max(mx, px);
            *ptr32++ = px;
        }

        #if 0
        /** vvvvvvvv hdr experiments. **/
        static const int kHdrHistSize = 2000;
        static int *hdr_hist_ = nullptr;
        if (hdr_hist_ == nullptr) {
            hdr_hist_ = new(std::nothrow) int[kHdrHistSize];
        }
        for (int i = 0; i < kHdrHistSize; ++i) {
            hdr_hist_[i] = 0;
        }
        ptr32 = (agm::int32 *) rgb32_.data;
        for (int i = 0; i < sz; ++i) {
            agm::int64 px = *ptr32++;
            px = px * (kHdrHistSize-1) / mx;
            px = std::max(agm::int64(0), std::min(px, agm::int64(kHdrHistSize-1)));
            ++hdr_hist_[px];
        }
        int hdr_count = 0;
        int hdr_thresh = 0;
        (void) hdr_thresh;
        for (int i = 0; i < kHdrHistSize; ++i) {
            int count = hdr_hist_[i];
            double range = (double(i) + 0.5) / double(kHdrHistSize);
            double pixels = double(hdr_count + count/2) / double(sz);
            hdr_count += count;
            if (range + pixels >= 1.0) {
                hdr_thresh = std::round(mx * range);
                //LOG("hdr i="<<i<<" range="<<range<<" pixels="<<pixels<<" thresh="<<hdr_thresh);
                break;
            }
        }
        /** ^^^^^^^^ hdr experiments. **/
        #endif

        /** scale and copy the 32 bit image back to the 16 bit buffer. **/
        ptr16 = (agm::uint16 *) rgb16_.data;
        ptr32 = (agm::int32 *) rgb32_.data;
        for (int i = 0; i < sz; ++i) {
            /** caution: overflow **/
            agm::int64 px = *ptr32++;
            /** vvvvvvvv hdr experiments **/
            /** scale bright pixels to the full visible range. **/
            //px = (px - hdr_thresh) * 65535 / (mx - hdr_thresh);
            /** scale dark pixels to the full visible range. **/
            //px = px * 65535 / hdr_thresh;
            /**
            scale bright pixels to the upper visible range.
            scale dark pixels to the lower visible range.
            **/
            /*if (px > hdr_thresh) {
                px = (px - hdr_thresh) * (65535/2)/ (mx - hdr_thresh) + (65535/2);
            } else {
                px = px * (65535/2) / hdr_thresh;
            }*/
            /** ^^^^^^^^ hdr experiments **/
            /** scale all pixels to the full visible range. **/
            px = px * 65535 / mx;
            px = std::max(agm::int64(0), std::min(px, agm::int64(65535)));
            *ptr16++ = px;
        }

        /** bump the counter and log. **/
        ++nstacked_;
        int ns = nstacked_;
        if (ns >= 10) {
            for(;;) {
                int rem = ns - ns / 10 * 10;
                if (rem != 0) {
                    break;
                }
                ns /= 10;
            }
            if (ns == 1 || ns == 3) {
                LOG("WindowThread Stacked "<<nstacked_<<" frames.");
            }
        }
    }

    void isoLinearScale() noexcept {
        int sz = 3 * img_->width_ * img_->height_;
        auto ptr = (agm::uint16 *) rgb16_.data;
        int iso = iso_;

        /** auto scale to maximum value. **/
        if (auto_iso_) {
            int mx = 0;
            for (int i = 0; i < sz; ++i) {
                int p = *ptr++;
                mx = std::max(mx, p);
            }
            if (mx == 0) {
                return;
            }

            /** update iso only if it's out of whack. **/
            int test = mx * iso / 100;
            if (test < 65535*9/10 || test > 65535) {
                int new_iso = 65535 * 100 / mx;
                iso = (9*iso + new_iso) / 10;
            }
        }

        /** sanity checks **/
        if (iso == 100) {
            return;
        }
        if (iso <= 0) {
            return;
        }

        /** iso scaling. **/
        ptr = (agm::uint16 *) rgb16_.data;
        for (int i = 0; i < sz; ++i) {
            int p = *ptr * iso / 100;
            p = std::min(p, 65535);
            *ptr++ = p;
        }

        /** update settings. **/
        if (auto_iso_ && iso != iso_) {
            iso_ = iso;
            LOG("new auto iso="<<iso);
            std::lock_guard<std::mutex> lock(settings_buffer_->mutex_);
            settings_buffer_->iso_ = iso;
        }
    }

    void gammaPowerScale() noexcept {
        double gamma = gamma_;
        if (gamma == 1.0) {
            return;
        }
        if (gamma <= 0) {
            return;
        }

        int sz = 3 * img_->width_ * img_->height_;
        auto ptr = (agm::uint16 *) rgb16_.data;

        for (int i = 0; i < sz; ++i) {
            int p = *ptr;
            double x = double(p) / 65535.0;
            x = std::pow(x, gamma);
            x *= 65535.0;
            p = (int) std::round(x);
            p = std::max(0, std::min(p, 65535));
            *ptr++ = p;
        }
    }

    void convertStdRgb() noexcept {
        /**
        it turns out...
        this camera really wants to be lit by sunlight.
        the halogen lighting in my office does bad things.
        specifically, blues and greens are indistinguishable.
        the below color correction matrix was derived from taking
        pictures of the computer screen with the halogen light on.
        i ran a different experiment with the macbeth color calibration chart.
        the colors were pretty darn good.
        the fisheye lens distorted things massively.
        so...
        the conclusion is we don't need to do anything to convert to standard rgb.
        amazing.
        **/
#if 0
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
#endif
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
        gamma_table_ = new(std::nothrow) agm::uint8[kGammaTableSize];

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
            gamma_table_[i] = ix;
        }
    }

    /**
    scale the source 16 bit components to the size of the gamma lookup table.
    set the destination 8 bit values.
    **/
    void applyDisplayGamma() noexcept {
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
            ix = std::max(0, std::min(ix, gamma_max_));

            /** use the value from the table. **/
            *dst++ = gamma_table_[ix];
        }
    }

    /** draw concentric circles to aid collimation. **/
    void showCollimationCircles() noexcept {
        if (show_circles_ == false) {
            return;
        }
        drawCircle(0.02);
        drawCircle(0.07);
        for (int i = 1; i <= 5; ++i) {
            double r = 0.16*double(i);
            drawCircle(r);
        }
    }

    /** draw one circle around the center of the image. **/
    void drawCircle(
        double r
    ) noexcept {
        int wd = img_->width_;
        int ht = img_->height_;
        int cx = wd / 2;
        int cy = ht / 2;
        int scale = std::min(cx, cy);
        int radius = (int) std::round(double(scale) * r);
        if (radius <= 0) {
            return;
        }

        double dx = double(cx) * circles_x_;
        double dy = double(cy) * circles_y_;
        cx += std::round(dx);
        cy += std::round(dy);

        int x = 0;
        int y = radius;
        int r42 = 4 * radius * radius;
        while (x <= y) {
            draw8Dots(cx, cy, x, y);
            /**
            the next point is x+1,y or x+1,y-1.
            the midpoint of those is x+1,y-0.5.
            if the midpoint is inside the circle then use y+1.
            otherwise use y.
            **/
            ++x;
            /**
            mid radius^2 = x^2 + (y-0.5)^2
            = x^2 + y^2 - y + 0.25
            **/
            int mr42 = 4*(x*x + y*y - y) + 1;
            if (mr42 >= r42) {
                --y;
            }
        }
    }

    /** draw 4 or 8 dots. **/
    void draw8Dots(
        int cx,
        int cy,
        int x,
        int y
    ) noexcept {
        drawDot(cx + x, cy - y);
        drawDot(cx + x, cy + y);
        if (x != 0) {
            drawDot(cx - x, cy - y);
            drawDot(cx - x, cy + y);
        }
        if (x != y) {
            drawDot(cx + y, cy + x);
            drawDot(cx - y, cy + x);
            if (x != 0) {
                drawDot(cx + y, cy - x);
                drawDot(cx - y, cy - x);
            }
        }
    }

    /** draw a red blended dot at the location. **/
    void drawDot(
        int x,
        int y
    ) noexcept {
        int wd = img_->width_;
        int ht = img_->height_;
        if (x < 0 || x >= wd) {
            return;
        }
        if (y < 0 || y >= ht) {
            return;
        }
        auto ptr = (agm::uint16 *) rgb16_.data;
        ptr += 3 * (wd * y + x) + 2;
        int p = ptr[0];
        p = (p + 0xFFFF) / 2;
        ptr[0] = p;
    }

    /** get the size of the default display. **/
    void getDisplayResolution() noexcept {
        /** this should be part of agm. **/
        auto display = XOpenDisplay(nullptr);
        auto screen = DefaultScreenOfDisplay(display);
        display_width_  = screen->width;
        display_height_ = screen->height;
        LOG("Display Resolution: "<<display_width_<<" x "<<display_height_);
    }

    /** crop the captured image if necessary. **/
    void setWindowCrop() noexcept {
        int max_usable_width = display_width_ * 80 / 100;
        int max_usable_height = display_height_ * 80 / 100;
        int image_width = img_->width_;
        int image_height = img_->height_;

        aoi_.x = 0;
        aoi_.y = 0;
        aoi_.width = image_width;
        aoi_.height = image_height;

        if (image_width > max_usable_width) {
            int margin = (image_width - max_usable_width) / 2;
            aoi_.x = margin;
            aoi_.width = max_usable_width;
        }
        if (image_height > max_usable_height) {
            int margin = (image_height - max_usable_height) / 2;
            aoi_.y = margin;
            aoi_.height = max_usable_height;
        }
    }

    /** save the image to the file. **/
    void saveImage() noexcept {
        if (save_file_name_.size()) {
            if (accumulate_) {
                saveAccumulatedImage();
            } else {
                saveDisplayImage();
            }
        } else if (raw_file_name_.size()) {
            saveRawImage();
        }
    }

    /** save the 8 bit gamma corrected image. **/
    void saveDisplayImage() noexcept {
        bool success = saveImage8();
        if (success) {
            LOG("CaptureThread Saved gamma corrected 8 bit image to file: "<<save_file_name_);
        }
    }

    /** save the 16 bit raw image. **/
    void saveRawImage() noexcept {
        bool success = saveImage16();
        if (success) {
            LOG("CaptureThread Saved raw image to 16 bit tiff file: "<<raw_file_name_);
        }
    }

    /** save the accumulated image and disable stacking. **/
    void saveAccumulatedImage() noexcept {
        bool success = saveImage32();
        if (success == false) {
            return;
        }

        LOG("CaptureThread Saved image to 32 bit tiff file: "<<save_file_name_);

        /** disable stacking. **/
        accumulate_ = 0;
        nstacked_ = 0;
        rgb32_ = 0;

        std::lock_guard<std::mutex> lock(settings_buffer_->mutex_);
        settings_buffer_->accumulate_ = false;
    }

    /** save the 8 bit image using opencv. **/
    bool saveImage8() noexcept {
        /** this is why you do not throw exceptions ever. **/\
        bool success = false;
        try {
            success = cv::imwrite(save_file_name_, rgb8_gamma_);
        } catch (const cv::Exception& ex) {
            LOG("CaptureThread Failed to save image to file: "<<save_file_name_<<" OpenCV reason: "<<ex.what());
        }
        return success;
    }

    /** save the raw 16 bit image using tiff. **/
    bool saveImage16() noexcept {
        /** create the tiff file. **/
        TIFF *tiff = TIFFOpen(raw_file_name_.c_str(), "w");
        if (tiff == nullptr) {
            LOG("CaptureThread Failed to create tiff file: "<<raw_file_name_);
            return false;
        }

        /** do some tiff things. **/
        int wd = img_->width_;
        int ht = img_->height_;
        LOG("wd="<<wd<<" ht="<<ht);
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, wd);
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, ht);
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
        TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
        TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);

        /** after we do the above. **/
        int default_strip_size = TIFFDefaultStripSize(tiff, 3 * wd);
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, default_strip_size);

        /** allocate a tiff-sized scanline. **/
        int scanline_size = TIFFScanlineSize(tiff);
        auto buffer = new(std::nothrow) char[scanline_size];

        /** zero the trailing bytes. **/
        int src_sz = 3 * sizeof(agm::int16) * wd;
        for (int i = src_sz; i < scanline_size; ++i) {
            buffer[i] = 0;
        }

        /** write all scanlines. **/
        bool success = true;
        auto src = (agm::int16 *) rgb16_.data;
        for (int y = 0; y < ht; ++y) {
            auto dst = (agm::int16 *) buffer;
            for (int x = 0; x < wd; ++x) {
                /** convert opencv BGR to tiff RGB. **/
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                src += 3;
                dst += 3;
            }
            int result = TIFFWriteScanline(tiff, buffer, y, 0);
            if (result < 0) {
                success = false;
                break;
            }
        }

        if (success == false) {
            LOG("CaptureThread Failed to write tiff file: "<<raw_file_name_);
        }

        delete[] buffer;
        TIFFClose(tiff);

        return success;
    }

    /** save the 32 bit image using tiff. **/
    bool saveImage32() noexcept {
        /** create the tiff file. **/
        TIFF *tiff = TIFFOpen(save_file_name_.c_str(), "w");
        if (tiff == nullptr) {
            LOG("CaptureThread Failed to create tiff file: "<<save_file_name_);
            return false;
        }

        /** do some tiff things. **/
        int wd = img_->width_;
        int ht = img_->height_;
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, wd);
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, ht);
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 32);
        TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
        TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
        TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

        /** after we do the above. **/
        int default_strip_size = TIFFDefaultStripSize(tiff, 3 * wd);
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, default_strip_size);

        /** allocate a tiff-sized scanline. **/
        int scanline_size = TIFFScanlineSize(tiff);
        auto buffer = new(std::nothrow) char[scanline_size];

        /** zero the trailing bytes. **/
        int src_sz = 3 * sizeof(agm::int32) * wd;
        for (int i = src_sz; i < scanline_size; ++i) {
            buffer[i] = 0;
        }

        /** find the max for scaling. **/
        int scale = 0;
        auto src = (agm::int32 *) rgb32_.data;
        int sz = 3 * ht * wd;
        for (int i = 0; i < sz; ++i) {
            int val = *src++;
            scale = std::max(scale, val);
        }

        /** write all scanlines. **/
        bool success = true;
        src = (agm::int32 *) rgb32_.data;
        for (int y = 0; y < ht; ++y) {
            auto dst = (agm::int32 *) buffer;
            for (int x = 0; x < wd; ++x) {
                /** convert opencv BGR to tiff RGB. **/
                dst[0] = scale32(src[2], scale);
                dst[1] = scale32(src[1], scale);
                dst[2] = scale32(src[0], scale);
                src += 3;
                dst += 3;
            }
            int result = TIFFWriteScanline(tiff, buffer, y, 0);
            if (result < 0) {
                success = false;
                break;
            }
        }

        if (success == false) {
            LOG("CaptureThread Failed to write tiff file: "<<save_file_name_);
        }

        delete[] buffer;
        TIFFClose(tiff);

        return success;
    }

    int scale32(
        int src,
        int scale
    ) noexcept {
        static const int kInt32Max = 0x7FFFFFFF;
        agm::int64 x = src;
        x *= kInt32Max;
        x /= scale;
        return (int) x;
    }
};
}

agm::Thread *createWindowThread(
    ImageDoubleBuffer *image_double_buffer,
    SettingsBuffer *settings_buffer
) noexcept {
    return new(std::nothrow) WindowThread(image_double_buffer, settings_buffer);
}
