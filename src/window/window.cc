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

class StarPosition {
public:
    double x_;
    double y_;
    int r_;
    double sum_x_;
    double sum_y_;
    double sum_;
};
typedef std::vector<StarPosition> StarPositions;

class WindowThread : public agm::Thread {
public:
    /** share data with the capture thread. **/
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    ImageBuffer *img_ = nullptr;
    /** share data with the menu thread. **/
    SettingsBuffer *settings_ = nullptr;
    bool accumulate_ = false;
    bool capture_black_ = false;
    int black_frames_ = 0;
    double black_mean_ = 0.0;
    double black_std_dev_ = 0.0;
    std::vector<int> bad_pixels_;
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
    bool find_stars_ = false;
    std::string save_file_name_;
    std::string raw_file_name_;
    std::string input_;

    /** our fields. **/
    cv::String win_name_ = "ZWO ASI";
    bool first_image_ = false;
    cv::Mat rgb16_;
    cv::Mat black_;
    cv::Mat rgb32_;
    cv::Mat cropped16_;
    cv::Mat gray16_;
    cv::Mat gray8_;
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
    bool logged_once_ = false;
    StarPositions star_positions_;
    cv::Mat median16_;
    std::vector<int> median_hist_;

    WindowThread(
        ImageDoubleBuffer *image_double_buffer,
        SettingsBuffer *settings_buffer
    ) noexcept : agm::Thread("WindowThread") {
        image_double_buffer_ = image_double_buffer;
        settings_ = settings_buffer;
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
            if (logged_once_ == false) {
                logged_once_ = true;
                LOG("WindowThread Received no image.");
            }
            wait_for_swap();
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

        /** fix bad pixels. **/
        fixBadPixels();

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

        /** balance colors. **/
        balanceColors();

        /** find stars. **/
        findStars();

        /** show histogram. **/
        showHistogram();

        /** show collimation circlex. **/
        showCollimationCircles();

        /** circle stars. **/
        showStars();

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

            /**
            append key to input.
            send it to the menu thread when complete.
            **/
            if (key == '\r') {
                key = '\n';
            }
            if (std::isprint(key) || key == '\n') {
                input_.push_back(key);
                if (key == '\n') {
                    std::lock_guard<std::mutex> lock(settings_->mutex_);
                    settings_->input_ += std::move(input_);
                }
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
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        accumulate_ = settings_->accumulate_;
        capture_black_ = settings_->capture_black_;
        balance_red_ = settings_->balance_red_;
        balance_blue_ = settings_->balance_blue_;
        exposure_ = settings_->exposure_;
        show_focus_ = settings_->show_focus_;
        gamma_ = settings_->gamma_;
        show_histogram_ = settings_->show_histogram_;
        auto_iso_ = settings_->auto_iso_;
        iso_ = settings_->iso_;
        show_circles_ = settings_->show_circles_;
        circles_x_ = settings_->circles_x_;
        circles_y_ = settings_->circles_y_;
        show_fps_ = settings_->show_fps_;
        find_stars_ = settings_->find_stars_;
        save_file_name_ = std::move(settings_->save_file_name_);
        raw_file_name_ = std::move(settings_->raw_file_name_);
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
        cropped16_ = rgb16_(ht_range, wd_range);
        /** =tsc= todo this converts by luminance. **/
        cv::cvtColor(cropped16_, gray16_, cv::COLOR_RGB2GRAY);
        cv::Laplacian(gray16_, laplace_, CV_64F, 3, 1, 0);
        cv::Scalar mean;
        cv::Scalar stddev;
        cv::meanStdDev(laplace_, mean, stddev);
        if (base_stddev_ == 0.0) {
            base_stddev_ = stddev[0];
        }
        auto blurriness = base_stddev_ / stddev[0];
        LOG("blurriness: "<<blurriness);
    }

    /**
    capture a sequence of black frames.
    **/
    void captureBlack() noexcept {
        if (capture_black_ == false) {
            processBlack();
            return;
        }

        /** the first black image. **/
        int wd = img_->width_;
        int ht = img_->height_;
        if (black_.rows == 0) {
            black_ = cv::Mat(ht, wd, CV_16UC1);
            bad_pixels_.resize(0);
        }

        /** the first black image of this set. **/
        if (black_frames_ == 0) {
            black_ = 0;
        }
        ++black_frames_;

        /** get the mean and standard deviation of the image. **/
        cv::Scalar mean;
        cv::Scalar stddev;
        cv::meanStdDev(img_->bayer_, mean, stddev);
        black_mean_ += mean[0];
        black_std_dev_ += stddev[0];

        /** accumulate black per pixel. don't overflow. **/
        int sz = wd * ht;
        auto pimg = (agm::uint16 *) img_->bayer_.data;
        auto pblk = (agm::uint16 *) black_.data;
        for (int i = 0; i < sz; ++i) {
            int blk = *pimg++ + *pblk;
            blk = std::min(blk, 65535);
            *pblk++ = blk;
        }

        LOG("Captured black frame "<<black_frames_);
    }

    void processBlack() noexcept {
        if (black_frames_ == 0) {
            return;
        }

        /** compute black mean and standard deviation. **/
        int round = black_frames_ / 2;
        black_mean_ = (black_mean_ + round) / black_frames_;
        black_std_dev_ = (black_std_dev_ + round) / black_frames_;
        LOG("Black mean="<<black_mean_<<" stdev="<<black_std_dev_);

        /** compute average black per pixel. **/
        int wd = img_->width_;
        int ht = img_->height_;
        int sz = wd * ht;
        auto pblk = (agm::uint16 *) black_.data;
        for (int i = 0; i < sz; ++i) {
            int blk = *pblk;
            if (blk < 65535L) {
                blk = (blk + round) / black_frames_;
                *pblk = blk;
            }
            ++pblk;
        }

        /**
        find bad pixels.
        they are more than 4 standard deviations too bright.
        **/
        int limit = std::round(black_mean_ + 4 * black_std_dev_);
        int mean = std::round(black_mean_);
        LOG("Bad pixel limit="<<limit);
        pblk = (agm::uint16 *) black_.data;
        int count = 0;
        for (int i = 0; i < sz; ++i) {
            int blk = *pblk;
            if (blk > limit) {
                /** change its black value to the mean. **/
                *pblk = mean;
                /** remember its location. **/
                bad_pixels_.push_back(i);

                /** log it. **/
                ++count;
                LOG("found bad pixel["<<count<<"] value="<<blk<<" at position="<<i);
            }
            ++pblk;
        }

        LOG("Captured "<<black_frames_<<" black frames.");
        black_frames_ = 0;
    }

    void fixBadPixels() noexcept {
        /** don't fix bad pixels if we're capturing black. **/
        if (capture_black_) {
            return;
        }

        /**
        use the average of its neighbors.
        unless it's at the top or bottom edge.
        then make it black.
        left and right can wrap around.
        cause lazy.
        **/
        int wd = img_->width_;
        int ht = img_->height_;
        int sz = wd * ht;
        int mean = std::round(black_mean_);
        auto pimg = (agm::uint16 *) img_->bayer_.data;
        for (auto pos : bad_pixels_) {
            /** source is bayer. **/
            int pos0 = pos - 2*wd;
            int pos1 = pos - 2;
            int pos2 = pos + 2;
            int pos3 = pos + 2*wd;
            if (pos0 < 0 || pos3 >= sz) {
                pimg[pos] = mean;
            } else {
                int p0 = pimg[pos0];
                int p1 = pimg[pos1];
                int p2 = pimg[pos2];
                int p3 = pimg[pos3];
                pimg[pos] = (p0 + p1 + p2 + p3 + 2) / 4;
            }
        }
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

        /** subtract black. assume same exposure time. **/
        int sz = img_->width_ * img_->height_;
        auto pimg = (agm::uint16 *) img_->bayer_.data;
        auto pblk = (agm::uint16 *) black_.data;
        for (int i = 0; i < sz; ++i) {
            /** 16 bits **/
            int pix = *pimg;
            int blk = *pblk++;
            pix -= blk;
            /** don't underflow. **/
            pix = std::max(0, pix);
            *pimg++ = pix;
        }
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
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->iso_ = iso;
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
        drawCircle(circles_x_, circles_y_, 0.02);
        drawCircle(circles_x_, circles_y_, 0.07);
        for (int i = 1; i <= 5; ++i) {
            double r = 0.16*double(i);
            drawCircle(circles_x_, circles_y_, r);
        }
    }

    /** draw a circle given center and radius. **/
    void drawCircle(
        double center_x,
        double center_y,
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

        double dx = double(cx) * center_x;
        double dy = double(cy) * center_y;
        cx += std::round(dx);
        cy += std::round(dy);

        drawCircle(cx, cy, radius);
    }

    /** draw a circle given center and radius. **/
    void drawCircle(
        int cx,
        int cy,
        int radius
    ) noexcept {
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

        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->accumulate_ = false;
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

    /**
    find stars.
    convert to flat grayscale where rgb are weighted equally.
    don't use opencv cvtColor.
    subtract the background using the local median.
    estimate noise.
    stars are brighter than the noise.
    we assume the stars are a symmetric normal distribution.

    find the brightest pixel.
    bright pixels are at least half as bright as the brightest pixel.
    find a bounding box.
    compute the centroid.
    we want to include 99% of the actual star pixels.
    we can afford to include background noise pixels.
    we assume noise is small and will cancel out.
    we expand the bounding box so 13% to 28% of the pixels int he box are bright.
    at that point the edges of the box are 2-3 sigma from the center.

    erase every pixel in the box.
    repeat.

    stars need to have a minimum number of bright pixels.
    the brightest pixel needs to be above the noise.

    some issues with this algorithm:

        paramaters feel ad hoc.

        sometimes a blob will be detected that touches or overlaps an existing star.
        one possibility is to ignore it.
        another possibility is to merge them.
        the bounding box should include the bounding box of both.
        which could be problematic.
        or not.
        which could also be problematic.

        sometimes we fail to find an obvious star.
        sometimes we get a lot of false positives.
        needs work.
    **/
    void findStars() noexcept {
        if (find_stars_ == false) {
            return;
        }

        /** find new stars. **/
        star_positions_.resize(0);

        /** configuration constants. **/
        static const double kThresholdStdDevs = 2.0;
        static const int kMaxRadius = 30;
        static const int kMaxCount = 12;
        static const int kAreaThreshold = 13;
        static const int kMinBrightCount = 5;

        /** need 16 bit grayscale. **/
        int wd = img_->width_;
        int ht = img_->height_;
        if (gray16_.rows == 0) {
            gray16_ = cv::Mat(ht, wd, CV_16UC1);
        }

        /** convert to grayscale. **/
        int sz = wd * ht;
        auto pgray16 = (agm::uint16 *) gray16_.data;
        auto pimg = (agm::uint16 *) rgb16_.data;
        for (int i = 0; i < sz; ++i) {
            int b = pimg[0];
            int g = pimg[1];
            int r = pimg[2];
            int px = (b + g + r + 2) / 3;
            *pgray16++ = px;
            pimg += 3;
        }

        /** find median. **/
        findMedianGrays();

        /** subtract the median. **/
        auto pmedian16 = (agm::uint16 *) median16_.data;
        pgray16 = (agm::uint16 *) gray16_.data;
        for (int i = 0; i < sz; ++i) {
            int med = *pmedian16++;
            int px = *pgray16;
            px -= med;
            px = std::max(0, px);
            *pgray16++ = px;
        }

        /** get the mean and standard deviation of the grayscale image. **/
        cv::Scalar mean;
        cv::Scalar stddev;
        cv::meanStdDev(gray16_, mean, stddev);
        int threshold = std::round(mean[0] + kThresholdStdDevs * stddev[0]);
        LOG("grayscale image mean="<<mean[0]<<" stddev="<<stddev[0]<< " threshold="<<threshold);

        for (int nstars = 0; nstars < kMaxCount; ++nstars) {
            /** find the maximum. **/
            int max_val = 0;
            int max_x = 0;
            int max_y = 0;
            auto pgray16_row = (agm::uint16 *) gray16_.data;
            pgray16_row += kMaxRadius * wd + kMaxRadius;
            for (int y = kMaxRadius; y < ht - kMaxRadius; ++y) {
                auto pgray16 = pgray16_row;
                for (int x = kMaxRadius; x < wd - kMaxRadius; ++x) {
                    int px = *pgray16++;
                    if (px > max_val) {
                        max_val = px;
                        max_x = x;
                        max_y = y;
                    }
                }
                pgray16_row += wd;
            }

            /** stop when it's below the threshold. **/
            if (max_val <= threshold) {
                LOG("Remaining star field is below threshold.");
                break;
            }

            /**
            find the brightest pixel.
            and the box containing the bright pixels around it.
            **/
            int half_height = (max_val + 1) / 2;
            int bright_pixels = 1;
            int square_radius = 1;
            int area = 1;
            for (; square_radius < kMaxRadius; ++square_radius) {
                bright_pixels += countBrightPixels(max_x, max_y, square_radius, half_height);
                int square_width = square_radius + 1 + square_radius;
                area = square_width * square_width;
                if (100 * bright_pixels <= kAreaThreshold * area) {
                    break;
                }
            }

            /** ignore micro blobs. **/
            if (bright_pixels >= kMinBrightCount) {
                /** compute centroid. **/
                StarPosition star;
                blobCentroid(star, max_x, max_y, square_radius);

                /** save the star. **/
                star_positions_.push_back(star);
                LOG("Found a star at "<<max_x<<","<<max_y<<" size="<<square_radius<<" max="<<max_val<<" area="<<area<<" count="<<bright_pixels<<".");
            } else {
                LOG("Skipped small blob at "<<max_x<<","<<max_y);
            }

            /** erase the blob. **/
            eraseBlob(max_x, max_y, square_radius);
        }

#if 0
        /** show it **/
        pgray16 = (agm::uint16 *) gray16_.data;
        pimg = (agm::uint16 *) rgb16_.data;
        for (int i = 0; i < sz; ++i) {
            int px = *pgray16++;
            pimg[0] = px;
            pimg[1] = px;
            pimg[2] = px;
            pimg += 3;
        }
#endif
    }

    int countBrightPixels(
        int cx,
        int cy,
        int r,
        int hh
    ) noexcept {
        int cnt = 0;
        int wd = img_->width_;
        //int ht = img_->height_;
        auto pgray16 = (agm::uint16 *) gray16_.data;

        int y = cy - r;
        for (int x = cx - r; x <= cx + r; ++x) {
            int px = pgray16[y*wd + x];
            if (px >= hh) {
                ++cnt;
            }
        }

        y = cy + r;
        for (int x = cx - r; x <= cx + r; ++x) {
            int px = pgray16[y*wd + x];
            if (px >= hh) {
                ++cnt;
            }
        }

        int x = cx - r;
        for (int y = cy - r + 1; y <= cy + r - 1; ++y) {
            int px = pgray16[y*wd + x];
            if (px >= hh) {
                ++cnt;
            }
        }

        x = cx + r;
        for (int y = cy - r + 1; y <= cy + r - 1; ++y) {
            int px = pgray16[y*wd + x];
            if (px >= hh) {
                ++cnt;
            }
        }

        return cnt;
    }

    void blobCentroid(
        StarPosition& star,
        int cx,
        int cy,
        int r
    ) noexcept {
        int x0 = cx - r;
        int x1 = cx + r;
        int y0 = cy - r;
        int y1 = cy + r;
        int wd = img_->width_;
        star.sum_x_ = 0.0;
        star.sum_y_ = 0.0;
        star.sum_ = 0.0;
        auto pgray16 = (agm::uint16 *) gray16_.data;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                double px = pgray16[y*wd + x];
                star.sum_x_ += double(x) * px;
                star.sum_y_ += double(y) * px;
                star.sum_ += px;
            }
        }
        star.x_ = star.sum_x_ / star.sum_;
        star.y_ = star.sum_y_ / star.sum_;
        star.r_ = r;
    }

    void eraseBlob(
        int cx,
        int cy,
        int r
    ) noexcept {
        int x0 = cx - r;
        int x1 = cx + r;
        int y0 = cy - r;
        int y1 = cy + r;
        int wd = img_->width_;
        auto pgray16 = (agm::uint16 *) gray16_.data;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                pgray16[y*wd + x] = 0;
            }
        }
    }

    void showStars() noexcept {
        if (find_stars_ == false) {
            return;
        }

        for (auto&& star : star_positions_) {
            int x = std::round(star.x_);
            int y = std::round(star.y_);
            drawCircle(x, y, star.r_);
        }
    }

    void findMedianGrays() noexcept {
        if (median_hist_.size() == 0) {
            median_hist_.resize(65536);
        }

        static const int kMedianRadius = 40;
        static const int kMedianWidth = kMedianRadius + 1 + kMedianRadius;
        static const int kMedianSize = kMedianWidth * kMedianWidth;
        static const int kMedianHalfSize = kMedianSize / 2;

        int wd = img_->width_;
        int ht = img_->height_;
        if (median16_.rows == 0) {
            median16_ = cv::Mat(ht, wd, CV_16UC1);
        }
        auto pgray16 = (agm::uint16 *) gray16_.data;
        auto pmedian16 = (agm::uint16 *) median16_.data;

        /**
        we are going to start at the top left.
        initialize the entire histogram.
        **/
        int sum0 = 0;
        int median = 0;
        for (int i = 0; i < 65536; ++i) {
            median_hist_[i] = 0;
        }
        for (int y = 0; y < kMedianWidth; ++y) {
            for (int x = 0; x < kMedianWidth; ++x) {
                int px = pgray16[wd * y + x];
                ++median_hist_[px];
            }
        }

        /**
        brute force find the median from the histogram.
        then we're going to step down one pixel.
        update the histogram by removing the top pixels.
        and adding the bottom pixels.
        slide the window up or down.
        step down another pixel.
        repeat until we reach the end of the column.
        then we're going to slide one pixel to the right.
        update the histogram by removing the left pixels.
        and adding the right pixels.
        repeat until we are done.
        **/
        int x0 = kMedianRadius;
        int x1 = wd - kMedianRadius - 1;
        int y0 = kMedianRadius;
        int y1 = ht - kMedianRadius - 1;
        int wx = x0;
        int wy = y0;
        int dir = +1;
        for(;;) {
            /** find the median efficiently. **/
            for(;;) {
                if (sum0 > kMedianHalfSize) {
                    --median;
                    sum0 -= median_hist_[median];
                    continue;
                }
                int sum1 = sum0 + median_hist_[median];
                if (sum1 <= kMedianHalfSize) {
                    sum0 = sum1;
                    ++median;
                    continue;
                }
                pmedian16[wd * wy + wx] = median;
                break;
            }

            /**
            increment or decrement y.
            switch direction at end of column.
            and go to the next column.
            **/
            int new_wy = wy + dir;
            if (new_wy >= y0 && new_wy <= y1) {
                /** somewhere in the middle of the column. move up or down. **/
                wy = new_wy;

                /** adjust the histogram. **/
                if (dir > 0) {
                    /** remove top pixels. add bottom pixels. **/
                    int x2 = wx - kMedianRadius;
                    int x3 = wx + kMedianRadius;
                    int y2 = wy - kMedianRadius - 1;
                    int y3 = wy + kMedianRadius;
                    for (int x = x2; x <= x3; ++x) {
                        int px = pgray16[wd * y2 + x];
                        if (px < median) {
                            --sum0;
                        }
                        --median_hist_[px];
                        px = pgray16[wd * y3 + x];
                        if (px < median) {
                            ++sum0;
                        }
                        ++median_hist_[px];
                    }
                } else {
                    /** remove bottom pixels. add top pixels. **/
                    int x2 = wx - kMedianRadius;
                    int x3 = wx + kMedianRadius;
                    int y2 = wy - kMedianRadius;
                    int y3 = wy + kMedianRadius + 1;
                    for (int x = x2; x <= x3; ++x) {
                        int px = pgray16[wd * y3 + x];
                        if (px < median) {
                            --sum0;
                        }
                        --median_hist_[px];
                        px = pgray16[wd * y2 + x];
                        if (px < median) {
                            ++sum0;
                        }
                        ++median_hist_[px];
                    }
                }
            } else {
                /** at end of column. switch direction and move right. **/
                dir = - dir;
                ++wx;
                if (wx > x1) {
                    break;
                }

                /** adjust the histogram. **/
                /** remove left pixels. add right pixels. **/
                int x2 = wx - kMedianRadius - 1;
                int x3 = wx + kMedianRadius;
                int y2 = wy - kMedianRadius;
                int y3 = wy + kMedianRadius;
                for (int y = y2; y <= y3; ++y) {
                    int px = pgray16[wd * y + x2];
                    if (px < median) {
                        --sum0;
                    }
                    --median_hist_[px];
                    px = pgray16[wd * y + x3];
                    if (px < median) {
                        ++sum0;
                    }
                    ++median_hist_[px];
                }
            }
        }

        /** last step. fill in the borders. **/
        x0 = kMedianRadius;
        x1 = wd - kMedianRadius;
        y0 = 0;
        y1 = kMedianRadius;
        for (int x = x0; x < x1; ++x) {
            int px = pmedian16[wd * y1 + x];
            for (int y = y0; y < y1; ++y) {
                pmedian16[wd * y + x] = px;
            }
        }
        x0 = kMedianRadius;
        x1 = wd - kMedianRadius;
        y0 = ht - kMedianRadius;
        y1 = ht;
        for (int x = x0; x < x1; ++x) {
            int px = pmedian16[wd * (y0-1) + x];
            for (int y = y0; y < y1; ++y) {
                pmedian16[wd * y + x] = px;
            }
        }
        x0 = 0;
        x1 = kMedianRadius;
        y0 = 0;
        y1 = ht;
        for (int y = y0; y < y1; ++y) {
            int px = pmedian16[wd * y + x1];
            for (int x = x0; x < x1; ++x) {
                pmedian16[wd * y + x] = px;
            }
        }
        x0 = wd - kMedianRadius;
        x1 = wd;
        y0 = 0;
        y1 = ht;
        for (int y = y0; y < y1; ++y) {
            int px = pmedian16[wd * y + x0-1];
            for (int x = x0; x < x1; ++x) {
                pmedian16[wd * y + x] = px;
            }
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
