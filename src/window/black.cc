/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
handle the capture of black frames.
subtract black from the source image.
and fix bad pixels.

this is most of the basic preprocessing of the image.

window thread.
**/

#include <opencv2/opencv.hpp>
#include <X11/Xlib.h>

#include <aggiornamento/master.h>

#include "window.h"


namespace WindowThread {

/**
capture a sequence of black frames.
**/
void WindowThread::captureBlack() noexcept {
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

void WindowThread::processBlack() noexcept {
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

void WindowThread::fixBadPixels() noexcept {
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
void WindowThread::subtractBlack() noexcept {
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

} // WindowThread
