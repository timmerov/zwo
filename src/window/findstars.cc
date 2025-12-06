/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
find the stars in the image.

window thread.
**/

#include <opencv2/opencv.hpp>

#include "levenberg_marquardt.h"
#include "window.h"


namespace WindowThread {

void WindowThread::findStars() noexcept {
    findStarsInImage();
    handleStarCommand();
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
void WindowThread::findStarsInImage() noexcept {
    /** do nothing if not finding stars. **/
    if (find_stars_ == false) {
        return;
    }

    /** find new stars. **/
    star_.positions_.resize(0);

    /** configuration constants. **/
    static const double kThresholdStdDevs = 0.0;
    static const int kMaxRadius = 30;
    static const int kMaxCount = 10;
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

    /** we don't need to do this if the median has already been subtracted. **/
    if (subtract_median_ == false) {
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
    }

    /** get the mean and standard deviation of the grayscale image. **/
    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(gray16_, mean, stddev);
    int threshold = std::round(mean[0] + kThresholdStdDevs * stddev[0]);
    //LOG("grayscale image mean="<<mean[0]<<" stddev="<<stddev[0]<< " threshold="<<threshold);

    /** find at most N stars. **/
    for(;;) {
        int nstars = star_.positions_.size();
        if (nstars >= kMaxCount) {
            break;
        }

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
        if (bright_pixels < kMinBrightCount) {
            /** erase the blob. **/
            eraseBlob(max_x, max_y, square_radius);

            LOG("Skipped small blob at "<<max_x<<","<<max_y);
            continue;
        }

        /** compute centroid. **/
        StarPosition star;
        blobCentroid(star, max_x, max_y, square_radius);
        star.brightness_ = max_val;

        /** erase the blob. **/
        eraseBlob(max_x, max_y, square_radius);

        auto existing_star = checkCollision(star);
        if (existing_star == nullptr) {
            /** save the star. **/
            star_.positions_.push_back(star);

            //LOG("Found star["<<nstars<<"] at "<<max_x<<","<<max_y<<" size="<<square_radius<<" max="<<max_val<<" area="<<area<<" count="<<bright_pixels<<".");
            continue;
        }

        /** expand the existing star's box. **/
        existing_star->left_ = std::min(existing_star->left_, star.left_);
        existing_star->top_ = std::min(existing_star->top_, star.top_);
        existing_star->right_ = std::max(existing_star->right_, star.right_);
        existing_star->bottom_ = std::max(existing_star->bottom_, star.bottom_);

        /** should we adjust the existing star's centroid? **/
        existing_star->sum_x_ += star.sum_x_;
        existing_star->sum_y_ += star.sum_y_;
        existing_star->sum_ += star.sum_;
        existing_star->x_ = existing_star->sum_x_ / existing_star->sum_;
        existing_star->y_ = existing_star->sum_y_ / existing_star->sum_;

        //LOG("Candidate collided with star at adjusted position: "<<existing_star->x_<<","<<existing_star->y_<<".");
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
    star.left_ = x0;
    star.top_ = y0;
    star.right_ = x1 + 1;
    star.bottom_ = y1 + 1;
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

/** they collide if they touch. **/
StarPosition *WindowThread::checkCollision(
    StarPosition& candidate
) noexcept {
    for (auto &&star : star_.positions_) {
        if (candidate.left_ <= star.right_
        &&  candidate.right_ > star.left_
        &&  candidate.top_ <= star.bottom_
        &&  candidate.bottom_ > star.top_) {
            return &star;
        }
    }
    return nullptr;
}

void WindowThread::showStars() noexcept {
    if (find_stars_ == false) {
        return;
    }

    for (auto &&star : star_.positions_) {
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

void WindowThread::handleStarCommand() noexcept {
    /** handle the star command. **/
    switch (star_command_) {
    case StarCommand::kNone:
    default:
        if (star_.building_list_) {
            addStarsToList();
        }
        break;

    case StarCommand::kBegin:
        beginStarList();
        break;

    case StarCommand::kCalculateCenter:
        calculateCenter();
        break;

    case StarCommand::kDelete:
        deleteStarList();
        break;

    case StarCommand::kDeleteAll:
        deleteAllStarLists();
        break;

    case StarCommand::kEnd:
        if (star_.building_list_) {
            addStarsToList();
        }
        endStarList();
        break;

    case StarCommand::kList:
        showStarLists();
        break;
    }
    star_command_ = StarCommand::kNone;
    star_param_ = 0;
}

void WindowThread::beginStarList() noexcept {
    /** end the current list without the current stars. **/
    if (star_.building_list_) {
        endStarList();
    }

    LOG("WindowThread star command: begin list");
    star_.building_list_ = true;
    StarPositions list;
    star_.lists_.push_back(list);
    addStarsToList();
}

void WindowThread::deleteStarList() noexcept {
    LOG("WindowThread star command: delete list["<<star_param_<<"]");
    int nlists = star_.lists_.size();
    if (nlists == 0) {
        LOG("WindowThread there are no star lists to delete.");
        return;
    }
    if (star_param_ < 0 || star_param_ >= nlists) {
        if (nlists == 0) {
            LOG("WindowThread list[0] is the only list.");
            return;
        }
        LOG("WindowThread please specify a list between 0 and "<<nlists-1);
        return;
    }
    star_.lists_.erase(star_.lists_.begin() + star_param_);
    LOG("WindowThread list["<<star_param_<<"] deleted");
}

void WindowThread::deleteAllStarLists() noexcept {
    LOG("WindowThread star command: delete all lists");
    star_.building_list_ = false;
    star_.lists_.clear();
}

void WindowThread::endStarList() noexcept {
    LOG("WindowThread star command: end list");
    star_.building_list_ = false;

    /** done if there are no lists. **/
    int nlists = star_.lists_.size();
    if (nlists == 0) {
        return;
    }

    /** delete the last list if empty. **/
    auto &list = star_.lists_.back();
    int nstars = list.size();
    if (nstars == 0) {
        list.pop_back();
        return;
    }

    /** copy all of the reliable stars to a new list. **/
    StarPositions reliable_list;
    for (auto &&star : list) {
        if (star.found_ > star.missed_) {
            reliable_list.push_back(star);
        }
    }

    /** sort the list by brightest first. **/
    auto brightest = [](
        const StarPosition &a, const StarPosition &b
    ) noexcept {
        return a.brightness_ > b.brightness_;
    };
    std::sort(reliable_list.begin(), reliable_list.end(), brightest);

    /** replace the star list. **/
    list = std::move(reliable_list);
}

void WindowThread::showStarLists() noexcept {
    LOG("WindowThread star command: show lists");
    int nlists = star_.lists_.size();
    for (int i = 0; i < nlists; ++i) {
        LOG("WindowThread star list["<<i<<"]:");
        auto &list = star_.lists_[i];
        int nstars = list.size();
        for (int k = 0; k < nstars; ++k) {
            auto &star = list[k];
            LOG("WindowThread Found star["<<k<<"] at "<<star.x_<<","<<star.y_<<" bright="<<star.brightness_<<" reliability="<<star.found_<<":"<<star.missed_);
        }
    }
}

void WindowThread::addStarsToList() noexcept {
    LOG("adding stars to list.");
    int nlists = star_.lists_.size();
    if (nlists <= 0) {
        return;
    }

    /** get the current list. **/
    auto &list = star_.lists_.back();

    /** copy the current list to an empty master list. */
    int nlist = list.size();
    if (nlist == 0) {
        LOG("copying stars to list.");
        list = star_.positions_;
        /** set the counts. **/
        for (auto &&star : list) {
            star.found_ = 1;
            star.missed_ = 0;
        }
        return;
    }

    /** get the total counts from the first star. **/
    int reliability = list[0].found_ + list[0].missed_;
    LOG("merging lists reliability="<<reliability);

    /**
    algorithm:
    for each star in the new list...
    find it in the old list.
        increment found but only once
    if not found
        add it to the list
        found = 1
        missed = reliability
    for each star in the old list
        if found + missed == reliability
        then increment missed

    finding a star means what?
    the bounding boxes overlap?
    **/
    for (auto &&candidate : star_.positions_) {
        bool found = false;
        for (auto &&star : list) {
            if (candidate.left_ < star.right_
            &&  candidate.right_ > star.left_
            &&  candidate.top_ < star.bottom_
            &&  candidate.bottom_ > star.top_) {
                int rel = star.found_ + star.missed_;
                if (rel <= reliability) {
                    ++star.found_;
                }
                found = true;
                break;
            }
        }
        if (found == false) {
            list.push_back(candidate);
            auto &added = list.back();
            added.found_ = 1;
            added.missed_ = reliability;
        }
    }
    for (auto &&star : list) {
        int rel = star.found_ + star.missed_;
        if (rel <= reliability) {
            ++star.missed_;
        }
    }
}

class CalculateCenter : public LevenbergMarquardt {
public:
    CalculateCenter() = default;
    virtual ~CalculateCenter() = default;

    /**
    params[0] = center x in pixels ~ 1000
    params[1] = center y in pixels ~ 1000
    params[2] = angle in arcseconds - 1000
    params[3]...
    **/
    static constexpr int kNParams = 3;

    /**
    givens[i] = x,y for N stars
    **/
    Eigen::VectorXd start_positions_;

    /**
    data points aka targets
    data points[i] = x,y for N*M stars
    **/

    /** configuration. **/
    static constexpr double kEpsilon = 0.1;
    static constexpr double kMinErrorChange = 0.001;

    static constexpr double kPi = 3.141592653589793238462643383279502884L;

    void run(
        const StarLists &lists
    ) noexcept {
        /** tsc: use the first two lists. **/
        auto &list0 = lists[0];
        auto &list1 = lists[1];
        int nstars = list0.size();

        /** mandatory. **/
        ndata_points_ = 2 * nstars;
        nparams_ = kNParams;
        /** configurations. **/
        verbosity_ = Verbosity::kQuiet;
        epsilon_ = kEpsilon;
        min_error_change_ = kMinErrorChange;

        /** set the initial guess. **/
        solution_.resize(kNParams);
        /** tsc: ooo ad hoc. **/
        solution_ << 0.0, 0.0, 0.0;

        start_positions_.resize(ndata_points_);
        targets_.resize(ndata_points_);

        /** x0,y0 **/
        for (int i = 0; i < nstars; ++i) {
            start_positions_[2*i] = list0[i].x_;
            start_positions_[2*i+1] = list0[i].y_;
        }

        /** x1,y1 **/
        for (int i = 0; i < nstars; ++i) {
            targets_[2*i] = list1[i].x_;
            targets_[2*i+1] = list1[i].y_;
        }

        /** solve for the params. **/
        solve();
    }

    virtual void makePrediction(
        const Eigen::VectorXd &solution,
        Eigen::VectorXd &predicted
    ) noexcept {

        /** extract the parameters. **/
        double cx = solution[0];
        double cy = solution[1];
        double arcs = solution[2];

        double degrees = arcs / 3600.0;
        double angle = degrees * kPi / 180.0;
        double sina = std::sin(angle);
        double cosa = std::cos(angle);

        /** calculate the target positions. **/
        for (int i = 0; i < ndata_points_; i += 2) {
            double x0 = start_positions_[i];
            double y0 = start_positions_[i+1];

            x0 -= cx;
            y0 -= cy;

            double x1 = x0 * cosa - y0 * sina;
            double y1 = x0 * sina + y0 * cosa;

            x1 += cx;
            y1 += cy;

            predicted[i] = x1;
            predicted[i+1] = y1;
        }
    }
};

void WindowThread::calculateCenter() noexcept {
    /** need 2+ star lists. **/
    int nlists = star_.lists_.size();
    if (nlists < 2) {
        LOG("WindowThread at least 2 star lists are needed to calculate the center.");
        return;
    }

    /** must be same size. **/
    auto& list0 = star_.lists_[0];
    auto& list1 = star_.lists_[1];
    int nstars0 = list0.size();
    int nstars1 = list1.size();
    if (nstars0 != nstars1) {
        LOG("WindowThread star list[0]:"<<nstars0<<" and list[1]:"<<nstars1<<" must be the same size.");
        return;
    }

    /** pair up the stars in the lists. **/
    for (int i = 0; i < nstars0; ++i) {
        auto &star0 = list0[i];
        double min_dist = 1e10;
        int min_idx = -1;
        for (int k = i; k < nstars1; ++k) {
            auto &star1 = list1[k];
            double dx = star0.x_ - star1.x_;
            double dy = star0.y_ - star1.y_;
            double dist = dx * dx + dy * dy;
            if (dist < min_dist) {
                min_dist = dist;
                min_idx = k;
            }
        }
        if (min_idx != i) {
            std::swap(list1[i], list1[min_idx]);
        }
    }

    /** do the math. **/
    CalculateCenter cc;
    cc.run(star_.lists_);

    /** center x,y is in cc.solution_[0,1] **/
    double center_x = cc.solution_[0];
    double center_y = cc.solution_[1];
    LOG("WindowThread calculated center is "<<center_x<<","<<center_y);
}


} // WindowThread
