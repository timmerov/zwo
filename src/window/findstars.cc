/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
find the stars in the image.

window thread.
**/

#include <opencv2/opencv.hpp>

#include "window.h"


namespace WindowThread {

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
void WindowThread::findStars() noexcept {
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

int WindowThread::countBrightPixels(
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

void WindowThread::blobCentroid(
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

void WindowThread::eraseBlob(
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

void WindowThread::showStars() noexcept {
    if (find_stars_ == false) {
        return;
    }

    for (auto&& star : star_positions_) {
        int x = std::round(star.x_);
        int y = std::round(star.y_);
        drawCircle(x, y, star.r_);
    }
}

void WindowThread::findMedianGrays() noexcept {
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

} // WindowThread
