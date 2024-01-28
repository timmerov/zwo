/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
archive os some polaris experimental code.
**/

cv::Mat polaris_;
cv::Mat polaris16_;
cv::Mat polaris_blur_;
cv::Mat polaris_diff_;

void experimentPolaris() noexcept {
    if (polaris_.rows == 0) {
        /** load the polaris 8 bit grayscale image. **/
        //auto fname = "data/polaris.png";
        auto fname = "data/polaris-45-left.bmp";
        //auto fname = "data/polaris-45-right.bmp";
        //auto fname = "data/corvus.jpeg";
        polaris_ = cv::imread(fname, cv::IMREAD_UNCHANGED);
        int wd = polaris_.cols;
        int ht = polaris_.rows;
        LOG("polaris wd="<<wd<<" ht="<<ht);

        /**
        convert to 16 bits.
        i don't know why opencv can't do this easily.
        **/
        polaris16_ = cv::Mat(ht, wd, CV_16UC1);
        int sz = wd * ht;
        {
            auto src = (agm::uint8 *) polaris_.data;
            auto dst = (agm::uint16 *) polaris16_.data;
            for (int i = 0; i < sz; ++i) {
                int x = (agm::uint32) src[0];
                x = x * 65535 / 255;
                dst[0] = x;
                ++src;
                ++dst;
            }
        }

        /** blur it. **/
        polaris_blur_ = cv::Mat(ht, wd, CV_16UC1);
        cv::GaussianBlur(polaris16_, polaris_blur_, {9,9}, 0.0);

        /**
        subtract the image from the blurred.
        apply threshold.
        **/
        polaris_diff_ = cv::Mat(ht, wd, CV_16UC1);
        {
            int threshold = 65535 * 20/100;
            auto src = (agm::uint16 *) polaris16_.data;
            auto blr = (agm::uint16 *) polaris_blur_.data;
            auto dst = (agm::uint16 *) polaris_diff_.data;
            for (int i = 0; i < sz; ++i) {
                int x = src[0];
                int b = blr[0];
                if (x < b || x < threshold) {
                    x = 0;
                }
                x = std::max(0, std::min(x, 65535));
                dst[0] = x;
                ++src;
                ++blr;
                ++dst;
            }
        }

        /** convert 16 bit grayscale to 16 bit rgb. **/
        rgb16_ = cv::Mat(ht, wd, CV_16UC3);
        cv::cvtColor(polaris_diff_, rgb16_, cv::COLOR_GRAY2RGB);
        rgb8_gamma_ = cv::Mat(ht, wd, CV_8UC3);
    }
    /** hack so applyGamma works correctly. **/
    img_->width_ = polaris_.cols;
    img_->height_ = polaris_.rows;
}
