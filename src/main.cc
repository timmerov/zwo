/*
Copyright (C) 2012-2023 tim cotter. All rights reserved.
*/

/**
drive the zwo asi astrophotography camera.
**/

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
            LOG("Number of cameras is "<<num_cameras_<<".");
            LOG("Expected number is 1.");
            return;
        }

        ASI_CAMERA_INFO camera_info;
		ASIGetCameraProperty(&camera_info, kCameraNumber);
		LOG("Found camera: "<<camera_info.Name);

        width_ = camera_info.MaxWidth;
        height_ = camera_info.MaxHeight;
        LOG("Resolution: "<<width_<<" x "<<height_);

        is_color_ = (camera_info.IsColorCam == ASI_TRUE);
        if (is_color_) {
            bayer_ = camera_info.BayerPattern;
            LOG("Color: Bayer: "<<bayer_);
        } else {
            LOG("Color: Mono");
        }

		ASI_ERROR_CODE result = ASIOpenCamera(kCameraNumber);
		if (result != ASI_SUCCESS) {
            LOG("Failed to open camera.");
            LOG("ASIOpenCamera("<<kCameraNumber<<"): "<<result);
            return;
		}
		LOG("Opened camera.");


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
