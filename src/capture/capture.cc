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


namespace {
class CaptureThread : public agm::Thread {
public:
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    ImageBuffer *img_ = nullptr;
    const int kCameraNumber = 0;
    int width_ = 0;
    int height_ = 0;

    CaptureThread(
        ImageDoubleBuffer *image_double_buffer
    ) noexcept : agm::Thread("CaptureThread") {
        image_double_buffer_ = image_double_buffer;
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
    	/** exposure time is in microseconds. **/
	    ASISetControlValue(kCameraNumber, ASI_EXPOSURE, 14*1000, ASI_FALSE);
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
    }

    virtual void runOnce() noexcept {
        /** ensure we have a buffer to read into. **/
        allocateBuffer();

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

    virtual void end() noexcept {
    	ASICloseCamera(kCameraNumber);
    	LOG("CaptureThread Closed camera.");
    }
};
}

agm::Thread *createCaptureThread(
    ImageDoubleBuffer *image_double_buffer
) noexcept {
    return new(std::nothrow) CaptureThread(image_double_buffer);
}
