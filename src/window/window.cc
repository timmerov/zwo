/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
display images in a window.
**/

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <tiffio.h>

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
    bool show_focus_ = false;
    bool show_histogram_ = false;
    bool show_fps_ = false;
    std::string save_file_name_;

    /** our fields. **/
    cv::String win_name_ = "ZWO ASI";
    bool first_image_ = false;
    cv::Mat rgb16_;
    cv::Mat rgb32_;
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
    int nstacked_ = 0;
    agm::int64 fps_start_ = 0;
    int fps_count_ = 0;

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

        /**
        convert the bayer image to rgb.
        despite the name RGB the format in memory is BGR.
        **/
        cv::cvtColor(img_->bayer_, rgb16_, cv::COLOR_BayerRG2RGB);

        /** check blurriness. **/
        checkBlurriness();

        /** capture black. **/
        captureBlack();

        /** subtract black. **/
        subtractBlack();

        /** stack images. **/
        stackImages();

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

        /** check for user hits escape key. **/
        int key = cv::waitKey(1);
        if (key == 27) {
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
        accumulate_ = settings_buffer_->accumulate_;
        capture_black_ = settings_buffer_->capture_black_;
        balance_red_ = settings_buffer_->balance_red_;
        balance_blue_ = settings_buffer_->balance_blue_;
        show_focus_ = settings_buffer_->show_focus_;
        show_histogram_ = settings_buffer_->show_histogram_;
        show_fps_ = settings_buffer_->show_fps_;
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
            ix = std::max(0, std::min(ix, gamma_max_));

            /** use the value from the table. **/
            *dst++ = gamma_[ix];
        }
    }

    /** save the image to the file. **/
    void saveImage() noexcept {
        if (save_file_name_.size() == 0) {
            return;
        }

        bool success = false;
        if (accumulate_) {
            success = saveImage32();
        } else {
            success = saveImage8();
        }

        if (success) {
            LOG("CaptureThread Saved image to file: "<<save_file_name_);

            /** disable stacking. **/
            if (accumulate_) {
                accumulate_ = 0;
                nstacked_ = 0;
                rgb32_ = 0;

                std::lock_guard<std::mutex> lock(settings_buffer_->mutex_);
                settings_buffer_->accumulate_ = false;
            }
        }
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
        LOG("wd="<<wd<<" ht="<<ht);
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, wd);
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, ht);
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 32);
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
        LOG("scale="<<scale);

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
        static const int kInt32Max = 0x7FFFFFF;
        agm::int64 x = src;
        x *= kInt32Max;
        x /= scale;
        return x;
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
};
}

agm::Thread *createWindowThread(
    ImageDoubleBuffer *image_double_buffer,
    SettingsBuffer *settings_buffer
) noexcept {
    return new(std::nothrow) WindowThread(image_double_buffer, settings_buffer);
}
