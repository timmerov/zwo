/**
open a random file of orion.
get and plot the signature of the belt stars.
**/

    void experimentOrion() noexcept {
        auto in_fname = "data/orionsbelt.png";
        auto out_fname = "signatures.bmp";
        auto orion_ = cv::imread(in_fname, cv::IMREAD_UNCHANGED);
        int wd = orion_.cols;
        int ht = orion_.rows;
        LOG("loaded file \""<<in_fname<<"\" "<<wd<<"x"<<ht);

        /**
        convert to 16 bits and undo the gamma.
        i don't know why opencv can't do this easily.
        **/
        auto orion16_ = cv::Mat(ht, wd, CV_16UC1);
        int sz = wd * ht;
        {
            int mx = 0;
            auto src = (agm::uint8 *) orion_.data;
            auto dst = (agm::uint16 *) orion16_.data;
            for (int i = 0; i < sz; ++i) {
                int x = (agm::uint32) src[0];
                int linear = gamma_max_;
                for (int k = 0; k <= gamma_max_; ++k) {
                    if (x <= gamma_[k]) {
                        linear = k;
                        break;
                    }
                }
                linear = linear * 65535 / gamma_max_;
                linear = std::max(0, std::min(linear, 65535));
                dst[0] = linear;
                ++src;
                ++dst;
                mx = std::max(mx, x);
            }
            LOG("orion16_ mx="<<mx);
        }

        /** blur it. **/
        auto orion_blur_ = cv::Mat(ht, wd, CV_16UC1);
        cv::GaussianBlur(orion16_, orion_blur_, {9,9}, 0.0);

        /**
        subtract the image from the blurred.
        apply threshold.
        **/
        auto orion_diff_ = cv::Mat(ht, wd, CV_16UC1);
        {
            int mx = 0;
            int threshold = 65535 * 2/100;
            auto src = (agm::uint16 *) orion16_.data;
            auto blr = (agm::uint16 *) orion_blur_.data;
            auto dst = (agm::uint16 *) orion_diff_.data;
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
                mx = std::max(mx, x);
            }
            LOG("orion_diff_ mx="<<mx);
        }

        /**
        the belt stars are:
        alnitak at 258 497
        alnilam at 464 364
        mintaka at 656 201
        nearest bright star is 200 pixels away.
        **/
        auto alnitak = getStarSignature(orion_diff_, 258, 497);
        auto alnilam = getStarSignature(orion_diff_, 464, 364);
        auto mintaka = getStarSignature(orion_diff_, 656, 201);
        plotStarSignature(orion_diff_, alnitak, 258, 497);
        plotStarSignature(orion_diff_, alnilam, 464, 364);
        plotStarSignature(orion_diff_, mintaka, 656, 201);

        /** convert 16 bit grayscale to 16 bit rgb. **/
        rgb16_ = cv::Mat(ht, wd, CV_16UC3);
        cv::cvtColor(orion_diff_, rgb16_, cv::COLOR_GRAY2RGB);
        {
            int mx = 0;
            int sz3 = 3 * sz;
            auto src = (agm::uint16 *) rgb16_.data;
            for (int i = 0; i < sz3; ++i) {
                int x = src[0];
                ++src;
                mx = std::max(mx, x);
            }
            LOG("rgb16_ mx="<<mx);
        }

        /** apply display gamma **/
        rgb8_gamma_ = cv::Mat(ht, wd, CV_8UC3);
        img_->width_ = wd;
        img_->height_ = ht;
        applyGamma();
        {
            int mx = 0;
            int sz3 = 3 * sz;
            auto src = (agm::uint8 *) rgb8_gamma_.data;
            for (int i = 0; i < sz3; ++i) {
                int x = src[0];
                ++src;
                mx = std::max(mx, x);
            }
            LOG("rgb8_gamma_ mx="<<mx);
        }
        LOG("gamma_max_="<<gamma_max_);
        /*{
            for (int i = 0; i < gamma_max_; ++i) {
                LOG("gamma_["<<i<<"]="<<(agm::uint32)(agm::uint8)gamma_[i]);
            }
        }*/

        /** save the image. **/
        cv::imwrite(out_fname, rgb8_gamma_);
        LOG("saved file \""<<out_fname<<"\"");
    }

    static const int kIdentifyRadius = 160;

    std::vector<agm::uint16> getStarSignature(
        cv::Mat &img,
        int h,
        int v
    ) noexcept {
        LOG("h="<<h<<" v="<<v);
        std::vector<agm::uint16> table(kIdentifyRadius);
        for (int i = 0; i < kIdentifyRadius; ++i) {
            table[i] = 0;
        }
        int minx = h - kIdentifyRadius;
        int maxx = h + kIdentifyRadius;
        int miny = v - kIdentifyRadius;
        int maxy = v + kIdentifyRadius;
        int wd = img.cols;
        int ht = img.rows;
        auto src = (agm::uint16 *) img.data;
        for (int y = miny; y <= maxy; ++y) {
            if (0 <= y && y < ht) {
                double dy = y - v + 0.5;
                double dy2 = dy * dy;
                auto ptr = src + y * wd;
                for (int x = minx; x <= maxx; ++x) {
                    if (0 <= x && x < wd) {
                        double dx = x - h + 0.5;
                        double dx2 = dx * dx;
                        double r2 = dx2 + dy2;
                        double r = std::sqrt(r2);
                        int idx = std::round(r);
                        if (0 <= idx && idx < kIdentifyRadius) {
                            int val = ptr[x];
                            int tbl = table[idx];
                            if (tbl < val) {
                                table[idx] = val;
                            }
                        }
                    }
                }
            }
        }
        return table;
    }

    void plotStarSignature(
        cv::Mat &img,
        std::vector<agm::uint16> &table,
        int h,
        int v
    ) noexcept {
        int minx = h - kIdentifyRadius;
        int maxx = h + kIdentifyRadius;
        int miny = v - kIdentifyRadius;
        int maxy = v + kIdentifyRadius;
        int wd = img.cols;
        int ht = img.rows;
        auto src = (agm::uint16 *) img.data;
        for (int y = miny; y <= maxy; ++y) {
            if (0 <= y && y < ht) {
                double dy = y - v + 0.5;
                double dy2 = dy * dy;
                auto ptr = src + y * wd;
                for (int x = minx; x <= maxx; ++x) {
                    if (0 <= x && x < wd) {
                        double dx = x - h + 0.5;
                        double dx2 = dx * dx;
                        double r2 = dx2 + dy2;
                        double r = std::sqrt(r2);
                        int idx = std::round(r);
                        if (0 <= idx && idx < kIdentifyRadius) {
                            int val = ptr[x];
                            int tbl = table[idx];
                            val = std::max(val, tbl);
                            ptr[x] = val;
                        }
                    }
                }
            }
        }
    }
