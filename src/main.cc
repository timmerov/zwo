/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
drive the zwo asi astrophotography camera.
**/

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>
#include <chrono>
#include <thread>

#include <ASICamera2.h>

#include "log.h"

namespace {
class Zwo {
public:
    int num_cameras_ = 0;
    const int kCameraNumber = 0;
    int width_ = 0;
    int height_ = 0;
    bool is_color_ = false;
    int bayer_ = 0;

    Zwo() = default;
    ~Zwo() = default;

    void run() {
        LOG("Starting ZWO ASI camera.");

        /** find the camera. **/
        num_cameras_ = ASIGetNumOfConnectedCameras();
        if (num_cameras_ != 1) {
            LOG("Aborting.");
            LOG("  Number of cameras is "<<num_cameras_<<".");
            LOG("  Expected number is 1.");
            return;
        }

        /** get the camera info. **/
        ASI_CAMERA_INFO camera_info;
		ASIGetCameraProperty(&camera_info, kCameraNumber);
		LOG("Found camera: "<<camera_info.Name);

        /** show max resolution. **/
        width_ = camera_info.MaxWidth;
        height_ = camera_info.MaxHeight;
        LOG("  Max resolution: "<<width_<<" x "<<height_);

        /** show color format. **/
        is_color_ = (camera_info.IsColorCam == ASI_TRUE);
        if (is_color_) {
            bayer_ = camera_info.BayerPattern;
            const char* bayer_types[] = {"RG", "BG", "GR", "GB"};
            LOG("  Color: Bayer ("<<bayer_<<"): "<<bayer_types[bayer_]);
        } else {
            LOG("  Color: Mono");
        }

        /** open the camera for capturing. **/
		auto result = ASIOpenCamera(kCameraNumber);
		if (result != ASI_SUCCESS) {
            LOG("Failed to open camera.");
            LOG("  ASIOpenCamera("<<kCameraNumber<<"): "<<result);
            return;
		}
		LOG("Opened camera.");

		/** initialize camera. **/
    	result = ASIInitCamera(kCameraNumber);
		if (result != ASI_SUCCESS) {
            LOG("Failed to initialize camera.");
            LOG("  ASIInitCamera("<<kCameraNumber<<"): "<<result);
            return;
        }
        LOG("Initialized camera.");

        /** enumerate all control capabilities and current settings. **/
        /*int num_controls = 0;
        ASIGetNumOfControls(kCameraNumber, &num_controls);
        for (int i = 0; i < num_controls; i++)
        {
            ASI_CONTROL_CAPS controls;
            ASIGetControlCaps(kCameraNumber, i, &controls);
            auto control_type = controls.ControlType;
            long control_value = controls.DefaultValue;
            auto control_auto = controls.IsAutoSupported;
            result = ASIGetControlValue(kCameraNumber, control_type, &control_value, &control_auto);
            if (result == ASI_SUCCESS) {
                LOG("Control["<<i<<"]: "<<controls.Name<<": value: "<<control_value<<" auto: "<<control_auto);
            } else {
                LOG("Control["<<i<<"]: "<<controls.Name<<": failure: "<<result);
            }
        }*/

        /** enumerate all supported binnings. **/
        /*std::stringstream supported_bins;
        const int kMaxBins = sizeof(camera_info.SupportedBins);
        for (int i = 0; i < kMaxBins; ++i) {
            int bin = camera_info.SupportedBins[i];
            if (bin == 0) {
                break;
            }
            supported_bins << " " << bin;
        }
        LOG("Supported binning modes:"<<supported_bins.str());*/

        /** gain 100 probably means no software gain. **/
    	ASISetControlValue(kCameraNumber, ASI_GAIN, 100, ASI_FALSE);
    	/** exposure time is in microseconds. **/
	    ASISetControlValue(kCameraNumber, ASI_EXPOSURE, 30*1000, ASI_FALSE);
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

        /** get the default resolution. **/
        int wd = 0;
        int ht = 0;
        int bin = 0;
        auto type = ASI_IMG_END;
        result = ASIGetROIFormat(kCameraNumber, &wd, &ht, &bin, &type);
        if (result == ASI_SUCCESS) {
            const char* image_types[] = {"raw8", "rgb24", "raw16", "y8"};
            LOG("Default format:");
            LOG("  resolution: "<<wd<<" x "<<ht);
            LOG("  bin: "<<bin);
            LOG("  image type: "<<image_types[type]<<" ("<<type<<")");
        } else {
            LOG("Failed to get default format.");
            LOG("  ASIGetROIFormat("<<kCameraNumber<<"): "<<result);
        }

        /** set camera parameters **/

        /** change color mode. **/
        LOG("Using Raw16.");
        int bytes_per_pixel = 2;
        int cv_type_bayer = CV_16UC1;
        int cv_type_rgb = CV_16UC3;
        type = ASI_IMG_RAW16;

        /** change binning. **/
        if (1) {
            LOG("Using bin=1");
            bin = 1;
        } else if(1) {
            LOG("Using bin=2");
            wd /= 2;
            ht /= 2;
            bin = 2;
        }
        result = ASISetROIFormat(kCameraNumber, wd, ht, bin, type);
        LOG("ASISetROIFormat("<<wd<<", "<<ht<<", "<<bin<<", "<<type<<") = "<<result);

        /** create a buffer for the camera image. **/
        int cam_sz = wd * ht * bytes_per_pixel;
        auto cam_buffer = new std::uint8_t[cam_sz];

        /** create a window. **/
        cv::String win_name = "ZWO ASI";
        cv::namedWindow(win_name);
        cv::moveWindow(win_name, 50, 50);

        /** create an image buffer for the window. **/
        cv::Size win_sz(wd, ht);
        cv::Mat win_image_bayer(win_sz, cv_type_bayer);
        cv::Mat win_image_rgb(win_sz, cv_type_rgb);

        /** show frames until use hits a key. **/
        for(;;) {
            /** start the exposure. wait for it to finish. **/
            auto status = ASI_EXP_WORKING;
            ASIStartExposure(kCameraNumber, ASI_FALSE);
            for(;;) {
                ASIGetExpStatus(kCameraNumber, &status);
                if (status != ASI_EXP_WORKING) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            //LOG("ASIGetExpStatus()="<<status);
            if (status == ASI_EXP_SUCCESS) {
                result = ASIGetDataAfterExp(kCameraNumber, cam_buffer, cam_sz);
                //LOG("ASIGetDataAfterExp()="<<result);
            }

            /** copy the bayer camera image to the window image. **/
            memcpy(win_image_bayer.data, cam_buffer, cam_sz);

            /** convert the bayer image to rgb. **/
            cv::cvtColor(win_image_bayer, win_image_rgb, cv::COLOR_BayerRG2RGB);

            /** show it. **/
            cv::imshow(win_name, win_image_rgb);

            /** wait for user input. **/
            int key = cv::waitKey(30);
            if (key >= 0) {
                LOG("key="<<key);
                break;
            }
        }

        /** clean up and go home. **/
        cv::destroyWindow(win_name);
        delete[] cam_buffer;
    	ASICloseCamera(kCameraNumber);
    	LOG("Closed camera.");
        LOG("Finished.");
    }
};
}

int main(
    int argc, char *argv[]
) noexcept {
    (void) argc;
    (void) argv;

    zwo_log::init("zwo.log");

    Zwo zwo;
    zwo.run();

    return 0;
}
