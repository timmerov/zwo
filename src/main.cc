/*
Copyright (C) 2012-2023 tim cotter. All rights reserved.
*/

/**
drive the zwo asi astrophotography camera.
**/

#include <sstream>

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

        num_cameras_ = ASIGetNumOfConnectedCameras();
        if (num_cameras_ != 1) {
            LOG("Aborting.");
            LOG("  Number of cameras is "<<num_cameras_<<".");
            LOG("  Expected number is 1.");
            return;
        }

        ASI_CAMERA_INFO camera_info;
		ASIGetCameraProperty(&camera_info, kCameraNumber);
		LOG("Found camera: "<<camera_info.Name);

        width_ = camera_info.MaxWidth;
        height_ = camera_info.MaxHeight;
        LOG("  Max resolution: "<<width_<<" x "<<height_);

        is_color_ = (camera_info.IsColorCam == ASI_TRUE);
        if (is_color_) {
            bayer_ = camera_info.BayerPattern;
            const char* bayer_types[] = {"RG", "BG", "GR", "GB"};
            LOG("  Color: Bayer ("<<bayer_<<"): "<<bayer_types[bayer_]);
        } else {
            LOG("  Color: Mono");
        }

		ASI_ERROR_CODE result = ASIOpenCamera(kCameraNumber);
		if (result != ASI_SUCCESS) {
            LOG("Failed to open camera.");
            LOG("  ASIOpenCamera("<<kCameraNumber<<"): "<<result);
            return;
		}
		LOG("Opened camera.");

        int num_controls = 0;
        ASIGetNumOfControls(kCameraNumber, &num_controls);
        for (int i = 0; i < num_controls; i++)
        {
            ASI_CONTROL_CAPS controls;
            ASIGetControlCaps(kCameraNumber, i, &controls);
            ASI_CONTROL_TYPE control_type = controls.ControlType;
            long control_value = controls.DefaultValue;
            ASI_BOOL control_auto = controls.IsAutoSupported;
            result = ASIGetControlValue(kCameraNumber, control_type, &control_value, &control_auto);
            if (result == ASI_SUCCESS) {
                LOG("Control["<<i<<"]: "<<controls.Name<<": value: "<<control_value<<" auto: "<<control_auto);
            } else {
                LOG("Control["<<i<<"]: "<<controls.Name<<": failure: "<<result);
            }
        }

        std::stringstream supported_bins;
        const int kMaxBins = sizeof(camera_info.SupportedBins);
        for (int i = 0; i < kMaxBins; ++i) {
            int bin = camera_info.SupportedBins[i];
            if (bin == 0) {
                break;
            }
            supported_bins << " " << bin;
        }
        LOG("Supported binning modes:"<<supported_bins.str());

        int wd = 0;
        int ht = 0;
        int bin = 0;
        ASI_IMG_TYPE type = ASI_IMG_END;
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

    zwo_log::init("rawsome.log");

    Zwo zwo;
    zwo.run();

    return 0;
}
