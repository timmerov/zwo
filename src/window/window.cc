/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
display images in a window.

window thread.
**/

#include <opencv2/opencv.hpp>
#include <X11/Xlib.h>

#include <aggiornamento/master.h>

#include "window.h"


namespace WindowThread {

WindowThread::WindowThread(
    ImageDoubleBuffer *image_double_buffer,
    SettingsBuffer *settings_buffer
) noexcept : agm::Thread("WindowThread") {
    image_double_buffer_ = image_double_buffer;
    settings_ = settings_buffer;
}

/*virtual*/ void WindowThread::begin() noexcept {
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
/*virtual*/ void WindowThread::runOnce() noexcept {
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
void WindowThread::wait_for_swap() noexcept {
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

/*virtual*/ void WindowThread::end() noexcept {
    cv::destroyWindow(win_name_);
    LOG("WindowThread Closed window.");
}

void WindowThread::copySettings() noexcept {
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
    save_path_ = std::move(settings_->save_path_);
}

void WindowThread::checkBlurriness() noexcept {
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

/** no alignment **/
void WindowThread::stackImages() noexcept {
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

void WindowThread::isoLinearScale() noexcept {
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

void WindowThread::gammaPowerScale() noexcept {
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

void WindowThread::balanceColors() noexcept {
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

void WindowThread::showHistogram() noexcept {
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

void WindowThread::plotHistogram(
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
void WindowThread::initGammaTable() noexcept {
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
void WindowThread::applyDisplayGamma() noexcept {
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
void WindowThread::showCollimationCircles() noexcept {
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
void WindowThread::drawCircle(
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
void WindowThread::drawCircle(
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
void WindowThread::draw8Dots(
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
void WindowThread::drawDot(
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
void WindowThread::getDisplayResolution() noexcept {
    /** this should be part of agm. **/
    auto display = XOpenDisplay(nullptr);
    auto screen = DefaultScreenOfDisplay(display);
    display_width_  = screen->width;
    display_height_ = screen->height;
    LOG("Display Resolution: "<<display_width_<<" x "<<display_height_);
}

/** crop the captured image if necessary. **/
void WindowThread::setWindowCrop() noexcept {
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

} // WindowThread

agm::Thread *createWindowThread(
    ImageDoubleBuffer *image_double_buffer,
    SettingsBuffer *settings_buffer
) noexcept {
    return new(std::nothrow) WindowThread::WindowThread(image_double_buffer, settings_buffer);
}
