/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
capture images from the zwo asi astrophotography camera.
**/

#include <ASICamera2.h>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/master.h>
#include <aggiornamento/thread.h>

#include <shared/image_double_buffer.h>
#include <shared/settings_buffer.h>


namespace {
class CaptureThread : public agm::Thread {
public:
    /** share data with the windows thread. **/
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    ImageBuffer *img_ = nullptr;
    /** share data with the menu thread. **/
    SettingsBuffer *settings_ = nullptr;
    bool auto_exposure_ = false;
    int exposure_ = 0;
    std::string load_file_name_;

    /** internal fields. **/
    static const int kCameraNumber = 0;
    int num_cameras_ = -1; /// unknown
    int width_ = 0;
    int height_ = 0;
    int over61_ = 0;
    int under61_ = 0;

    CaptureThread(
        ImageDoubleBuffer *image_double_buffer,
        SettingsBuffer *settings_buffer
    ) noexcept : agm::Thread("CaptureThread") {
        image_double_buffer_ = image_double_buffer;
        settings_ = settings_buffer;
    }

    virtual ~CaptureThread() = default;

    virtual void begin() noexcept {
        LOG("CaptureThread.");
        /**
        capture thread starts with buffer 0.
        window thread starts with buffer 1.
        **/
        img_ = image_double_buffer_->acquire(0);

        /** start with an exposure of 20 milliseconds. **/
        exposure_ = 20 * 1000;
    }

    void init_camera() noexcept {
        /** find the camera. **/
        int org_num_cameras = num_cameras_;
        num_cameras_ = ASIGetNumOfConnectedCameras();

        /** no cameras. look again later. **/
        if (num_cameras_ == 0) {
            if (org_num_cameras < 0) {
                LOG("No camera found.");
            }
            return;
        }

        /** more than one camera. abort. **/
        if (num_cameras_ != 1) {
            LOG("CaptureThread Aborting.");
            LOG("  Number of cameras is "<<num_cameras_<<".");
            LOG("  Expected number is 1.");
            agm::master::setDone();
            return;
        }

        /** get the camera info. **/
        ASI_CAMERA_INFO camera_info;
		ASIGetCameraProperty(&camera_info, kCameraNumber);
		LOG("CaptureThread Found camera: "<<camera_info.Name);

        /** show max resolution. **/
        width_ = camera_info.MaxWidth;
        height_ = camera_info.MaxHeight;
        LOG("CaptureThread Max resolution: "<<width_<<" x "<<height_);

        /** show color format. **/
        bool is_color = (camera_info.IsColorCam == ASI_TRUE);
        if (is_color == false) {
            LOG("CaptureThread Aborting.");
            LOG("  Camera is not color.");
            agm::master::setDone();
            return;
        }
        int bayer_ = camera_info.BayerPattern;
        const char* bayer_types[] = {"RGGB", "BGGR", "GRBG", "GBRG"};
        LOG("CaptureThread Bayer ("<<bayer_<<"): "<<bayer_types[bayer_]);

        /** open the camera for capturing. **/
		auto result = ASIOpenCamera(kCameraNumber);
		if (result != ASI_SUCCESS) {
            LOG("Failed to open camera.");
            LOG("  ASIOpenCamera("<<kCameraNumber<<"): "<<result);
            agm::master::setDone();
            return;
		}
		LOG("CaptureThread Opened camera.");

		/** initialize camera. **/
    	result = ASIInitCamera(kCameraNumber);
		if (result != ASI_SUCCESS) {
            LOG("CaptureThread Aborting.");
            LOG("  Failed to initialize camera.");
            LOG("  ASIInitCamera("<<kCameraNumber<<"): "<<result);
            agm::master::setDone();
            return;
        }
        LOG("CaptureThread Initialized camera.");

        /** gain 100 probably means no software gain. **/
    	ASISetControlValue(kCameraNumber, ASI_GAIN, 100, ASI_FALSE);
	    /** the scale seems to be 1 to 99 relative to green. defaults are 52,95. **/
    	ASISetControlValue(kCameraNumber, ASI_WB_R, 52, ASI_FALSE);
    	ASISetControlValue(kCameraNumber, ASI_WB_B, 95, ASI_FALSE);
    	/** no reason to set usb transfer speed to less than 100%. **/
        ASISetControlValue(kCameraNumber, ASI_BANDWIDTHOVERLOAD, 100, ASI_FALSE);
        /** no flipping. **/
        ASISetControlValue(kCameraNumber, ASI_FLIP, 0, ASI_FALSE);
        /** these auto settings should not be in use by the camera. **/
        ASISetControlValue(kCameraNumber, ASI_AUTO_MAX_GAIN, 0, ASI_FALSE);
        ASISetControlValue(kCameraNumber, ASI_AUTO_MAX_EXP, 0, ASI_FALSE);
        ASISetControlValue(kCameraNumber, ASI_AUTO_TARGET_BRIGHTNESS, 0, ASI_FALSE);
        /** no idea what high speed mode is. **/
        ASISetControlValue(kCameraNumber, ASI_HIGH_SPEED_MODE, 0, ASI_FALSE);
        /** no idea what mono binning is. **/
        ASISetControlValue(kCameraNumber, ASI_MONO_BIN, 0, ASI_FALSE);

        /** change color mode. **/
        LOG("CaptureThread Using Raw16.");
        int bin = 1;
        auto type = ASI_IMG_RAW16;
        result = ASISetROIFormat(kCameraNumber, width_, height_, bin, type);
        LOG("CaptureThread ASISetROIFormat("<<width_<<", "<<height_<<", "<<bin<<", "<<type<<") = "<<result);
		if (result != ASI_SUCCESS) {
            LOG("CaptureThread Aborting.");
            LOG("  Failed to set resolution and format.");
            agm::master::setDone();
            return;
        }
    }

    virtual void runOnce() noexcept {
        if (num_cameras_ <= 0) {
            init_camera();
        }
        if (isRunning() == false) {
            return;
        }

        /** copy all of the settings at once. **/
        copySettings();

        /** mabye load a file. **/
        if (load_file_name_.size() > 0) {
            loadImageFromFile();
        }

        /** maybe transfer an image from the camera. **/
        else if (num_cameras_ > 0) {
            transferImageFromCamera();
        }

        /** maybe snooze for a bit. **/
        else {
            agm::sleep::milliseconds(100);
        }
    }

    void transferImageFromCamera() noexcept {
        /** ensure we have a buffer to read into. **/
        allocateBuffer();

    	/** exposure time is in microseconds. **/
	    ASISetControlValue(kCameraNumber, ASI_EXPOSURE, exposure_, ASI_FALSE);

        /** capture an image. **/
        auto status = ASI_EXP_WORKING;
        ASIStartExposure(kCameraNumber, ASI_FALSE);
        for(;;) {
            ASIGetExpStatus(kCameraNumber, &status);
            if (status != ASI_EXP_WORKING) {
                break;
            }
            if (isRunning() == false) {
                ASIStopExposure(kCameraNumber);
                LOG("CaptureThread capture stopped.");
                return;
            }
            agm::sleep::milliseconds(10);
        }
		auto result = ASI_ERROR_END;
        if (status == ASI_EXP_SUCCESS) {
            result = ASIGetDataAfterExp(kCameraNumber, img_->bayer_.data, img_->bytes_);
        }
        if (status != ASI_EXP_SUCCESS || result != ASI_SUCCESS) {
            LOG("CaptureThread capture failed.");
            LOG("  ASIGetExpStatus() = "<<status);
            LOG("  ASIGetDataAfterExp() = "<<result);
            LOG("Assume camera was unplugged.");
            LOG("Closing camera.");
        	ASICloseCamera(kCameraNumber);
        	num_cameras_ = 0;
            return;
        }

        /** adjust the exposure time. **/
        autoAdjustExposure();

        img_ = image_double_buffer_->swap(img_);
    }

    void loadImageFromFile() noexcept {
        LOG("CaptureThread Loading file \""<<load_file_name_<<"\".");
        cv::Mat img = cv::imread(load_file_name_, cv::IMREAD_COLOR | cv::IMREAD_ANYDEPTH);

        if (img.empty()) {
            LOG("Failed to read file.");
            return;
        }

        int wd = img.cols;
        int ht = img.rows;
        int sz = img.elemSize1();
        cv::Mat img16;
        if (sz == 2) {
            img16 = img;
        } else {
            img.convertTo(img16, CV_16UC3, 257);
        }
        int bits = 8 * sz;
        LOG("Image is "<<wd<<"x"<<ht<<" by "<<bits<<" bits.");

        /** convert BGR to bayer RGGB format. **/
        cv::Mat bayer(ht, wd, CV_16UC1);
        auto pimg = (agm::uint16 *) img16.data;
        auto pbayer = (agm::uint16 * ) bayer.data;
        int iwd = wd * 3;
        for (int y = 0; y < ht; y += 2) {
            for (int x = 0; x < wd; x += 2) {
                /** get 4 reds : bgR bgR / bgR bgR **/
                int r0 = pimg[2];
                int r1 = pimg[3+2];
                int r2 = pimg[iwd+2];
                int r3 = pimg[iwd+3+2];
                int r = (r0 + r1 + r2 + r3 + 2) / 4;
                /** set red : Rg / gb **/
                pbayer[0] = r;

                /** get 4 greens : bGr bGr / bGr bGr **/
                int g0 = pimg[1];
                int g1 = pimg[3+1];
                int g2 = pimg[iwd+1];
                int g3 = pimg[iwd+3+1];
                int g = (g0 + g1 + g2 + g3 + 2) / 4;
                /** set greens : rG / Gb **/
                pbayer[1] = g;
                pbayer[wd] = g;

                /** get 4 blues : Bgr Bgr / Bgr Bgr **/
                int b0 = pimg[0];
                int b1 = pimg[3];
                int b2 = pimg[iwd];
                int b3 = pimg[iwd+3];
                int b = (b0 + b1 + b2 + b3 + 2) / 4;
                /** set blue : rg / gB **/
                pbayer[wd+1] = b;

                /** bump pointers by two pixels. **/
                pimg += 3*2;
                pbayer += 2;
            }

            /** bump pointers by one row. **/
            pimg += iwd;
            pbayer += wd;
        }

        /** do the things. **/
        width_ = wd;
        height_ = ht;
        allocateBuffer();
        img_->bayer_ = std::move(bayer);
        img_ = image_double_buffer_->swap(img_);
    }

    virtual void end() noexcept {
        if (num_cameras_ == 1) {
        	ASICloseCamera(kCameraNumber);
        }
    	LOG("CaptureThread Closed camera.");
    }

    void allocateBuffer() noexcept {
        if (img_->width_) {
            return;
        }

        const int kBytesPerPixel = 2;
        int sz = width_ * height_;
        img_->width_ = width_;
        img_->height_ = height_;
        img_->bytes_ = kBytesPerPixel * sz;
        img_->bayer_ = std::move(cv::Mat(height_, width_, CV_16UC1));
    }

    void copySettings() noexcept {
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        auto_exposure_ = settings_->auto_exposure_;
        exposure_ = settings_->exposure_;
        load_file_name_ = std::move(settings_->load_file_name_);
    }

    void writeSettings() noexcept {
        /** only update exposure if auto exposure is enabled. **/
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        if (settings_->auto_exposure_) {
            settings_->exposure_ = exposure_;
        }
    }

    /**
    adjust the exposure so the largest value is equally likely
    to be above or below 61000.
    which is chosen fairly arbitrarily.
    **/
    void autoAdjustExposure() noexcept {
        if (auto_exposure_ == false) {
            over61_ = 0;
            under61_ = 0;
            return;
        }

        /** find the maximum pixel value in the image. **/
        int sz = width_ * height_;
        int hi = 0;
        auto src = (agm::uint16 *) img_->bayer_.data;
        for (int i = 0; i < sz; ++i) {
            int x = *src++;
            hi = std::max(hi, x);
        }

        /** some panics. **/
        if (hi == 0) {
            return;
        }
        if (hi < 50000) {
            /** guard against overflow. **/
            agm::int64 x = exposure_;
            x = x * 56000 / hi;
            /** arbitrarily limit auto exposure to 100 us and 30s. **/
            static const agm::int64 k100us = 100;
            static const agm::int64 k30s = 30*1000*1000;
            x = std::max(k100us, std::min(x, k30s));
            exposure_ = x;
            LOG("new auto exposure="<<exposure_);
            writeSettings();
        }

        /** the counts decay over time. **/
        over61_ = over61_ * 97/100;
        under61_ = under61_ * 97/100;

        /** increase either the over or under count. **/
        if (hi > 61000) {
            over61_ += 10;
        } else {
            under61_ += 10;
        }

        /** no adjustment this frame. **/
        if (over61_ < 100 && under61_ < 100) {
            return;
        }

        /** take no step if they're really close. **/
        if (over61_ <= 90 || under61_ <= 90) {
            /** take a big step of about 10% of the exposure. **/
            int step;
            step = exposure_ / 10;

            /** unless, we're already close to the correct value. **/
            if (over61_ > 5 && under61_ > 5) {
                step /= 30;
            }

            /** must make an adjustment. **/
            if (step == 0) {
                step = 1;
            }

            /** step the correct direction. **/
            if (over61_ >= 100) {
                step = - step;
            }

            /** take the step. **/
            exposure_ += step;
            LOG("new auto exposure="<<exposure_);

            writeSettings();
        }

        /** reset the over/under counts. **/
        int big = std::max(over61_, under61_);
        over61_ = over61_ * 90 / big;
        under61_ = under61_ * 90 / big;
    }
};
}

agm::Thread *createCaptureThread(
    ImageDoubleBuffer *image_double_buffer,
    SettingsBuffer *settings_buffer
) noexcept {
    return new(std::nothrow) CaptureThread(image_double_buffer, settings_buffer);
}
