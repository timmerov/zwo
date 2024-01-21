/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
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
    SettingsBuffer *settings_buffer_ = nullptr;

    /** internal fields. **/
    static const int kCameraNumber = 0;
    int width_ = 0;
    int height_ = 0;
    bool auto_exposure_ = false;
    int exposure_ = 0;
    int over61_ = 0;
    int under61_ = 0;

    CaptureThread(
        ImageDoubleBuffer *image_double_buffer,
        SettingsBuffer *settings_buffer
    ) noexcept : agm::Thread("CaptureThread") {
        image_double_buffer_ = image_double_buffer;
        settings_buffer_ = settings_buffer;
    }

    virtual ~CaptureThread() = default;

    virtual void begin() noexcept {
        LOG("CaptureThread.");
        /**
        capture thread starts with buffer 0.
        window thread starts with buffer 1.
        **/
        img_ = image_double_buffer_->acquire(0);

        /** find the camera. **/
        int num_cameras = ASIGetNumOfConnectedCameras();
        if (num_cameras != 1) {
            LOG("CaptureThread Aborting.");
            LOG("  Number of cameras is "<<num_cameras<<".");
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
    	/** no idea what this is. something about usb transfer speed. **/
        ASISetControlValue(kCameraNumber, ASI_BANDWIDTHOVERLOAD, 40, ASI_FALSE);
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

        /** start with an exposure of 1 millisecond. **/
        exposure_ = 100;// 1 * 1000;
    }

    virtual void runOnce() noexcept {
        /** ensure we have a buffer to read into. **/
        allocateBuffer();

        /** copy all of the settings at once. **/
        copySettings();

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
            agm::sleep::milliseconds(1);
        }
		auto result = ASI_ERROR_END;
        if (status == ASI_EXP_SUCCESS) {
            result = ASIGetDataAfterExp(kCameraNumber, img_->bayer_.data, img_->bytes_);
        }
        if (result != ASI_SUCCESS) {
            LOG("CaptureThread Aborting.");
            LOG("  Failed to capture image.");
            LOG("  ASIGetDataAfterExp() = "<<result);
            agm::master::setDone();
            return;
        }

        /** adjust the exposure time. **/
        autoAdjustExposure();

        img_ = image_double_buffer_->swap(img_);
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
        std::lock_guard<std::mutex> lock(settings_buffer_->mutex_);
        auto_exposure_ = settings_buffer_->auto_exposure_;
        exposure_ = settings_buffer_->exposure_;
    }

    void writeSettings() noexcept {
        std::lock_guard<std::mutex> lock(settings_buffer_->mutex_);
        settings_buffer_->exposure_ = exposure_;
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
        if (hi < 50000) {
            exposure_ = exposure_ * 56000 / hi;
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

    virtual void end() noexcept {
    	ASICloseCamera(kCameraNumber);
    	LOG("CaptureThread Closed camera.");
    }
};
}

agm::Thread *createCaptureThread(
    ImageDoubleBuffer *image_double_buffer,
    SettingsBuffer *settings_buffer
) noexcept {
    return new(std::nothrow) CaptureThread(image_double_buffer, settings_buffer);
}
