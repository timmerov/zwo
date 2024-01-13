/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
drive the zwo asi astrophotography camera.

data dump:
led display frequencies:
700nm R
546nm G
436nm B

the sun is not an average star.
but it's pretty close to the joint index.
ie 10% of stars are a LOT brighter.
and 90% of stars are a somewhat dimmer.
the visible spectrum is from 380nm to 780nm.
roughly 2x wavelength.
irradiance at 380 is about 1500 W/m^2.
and at 780 is about 1300 W/m^2.
but that's energy.
cameras are counting photons.
there are 2x as many photons per watt at 380 than at 780.
so there are about 2.3x as many photons from the sun at 380 than at 780.

the zwo has a sensitivity graph that's roughly as follows:

wavelength  red     green   blue
400         0.09    0.08    0.40
425         0.06    0.06    0.52
450         0.03    0.08    0.65
475         0.04    0.40    0.65
500         0.05    0.83    0.43
525         0.09    0.93    0.21
550         0.08    0.90    0.10
575         0.40    0.80    0.06
600         1.00    0.52    0.04
625         0.97    0.25    0.04
650         0.96    0.19    0.08
675         0.83    0.24    0.10
700         0.80    0.35    0.10
725         0.82    0.38    0.08
750         0.75    0.40    0.08
775         0.69    0.45    0.15
800         0.60    0.48    0.42

the original idea was to use this data to divine the color conversion matrix.
ha! it didn't work worth doo doo.

so i collimated the camera and pointed it at the computer screen.
it was very out of focus.
i showed the camera black red green and blue.
and printed the response.
that gave a matrix that converted actual rgb to observed rgb.
caution here: it's easy to transpose the matrix.
invert the matrix to get the color conversion matrix from observed rgb to actual rgb.
here's what i got:
    // subtract black.
    r0 -= 871;
    g0 -= 1259;
    b0 -= 1799;
    // convert observed rgb to actual srgb.
    int r1 = (100*r0 -  16*g0 -   0*b0)/100;
    int g1 = (-22*r0 +  63*g0 -   8*b0)/100;
    int b1 = (  2*r0 -  33*g0 +  83*b0)/100;
images captured from the screen are reasonable.
over-exposed is pink.
and there's no gamma correction.
images captored from elsewhere are very very red.
maybe it's picking up a lot more infrared from the display?

this matrix isn't bad for real images.
(once i fixed the transpose error.)
it comes from the sensitivity graph.
    int r1 = ( 79*r0 -  32*g0 +   1*b0)/100;
    int g1 = (-32*r0 +  83*g0 -   2*b0)/100;
    int b1 = ( -9*r0 -  18*g0 + 100*b0)/100;

this matrix is not as good as the previous.
it's more blue.
it comes from weighting the sensitivity graph by the relative energy flux of the sun at each frequency.
    int r1 = ( 61*r0 -  23*g0 +   0*b0)/100;
    int g1 = (-28*r0 +  76*g0 -  18*b0)/100;
    int b1 = ( -7*r0 -  19*g0 + 100*b0)/100;

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

        /** create a window. **/
        cv::String win_name = "ZWO ASI";
        cv::namedWindow(win_name);
        cv::moveWindow(win_name, 50, 50);

        /** we can load the camera image directly into a cv bayer array. **/
        cv::Size win_sz(wd, ht);
        int bayer_sz = wd * ht * bytes_per_pixel;
        cv::Mat bayer(win_sz, cv_type_bayer);

        /** buffer to convert to rgb using cv. **/
        cv::Mat cam_rgb48(win_sz, cv_type_rgb);

        /** buffer for focus. **/
        cv::Mat focus1(win_sz, CV_16UC1);
        cv::Mat focus2(win_sz, CV_16UC1);

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
                result = ASIGetDataAfterExp(kCameraNumber, bayer.data, bayer_sz);
                //LOG("ASIGetDataAfterExp()="<<result);
            }

            /**
            convert the bayer image to rgb.
            despite the name RGB the format in memory is BGR.
            **/
            cv::cvtColor(bayer, cam_rgb48, cv::COLOR_BayerRG2RGB);

            /** check blurriness **/
            /*cv::cvtColor(cam_rgb48, focus1, cv::COLOR_RGB2GRAY);
            cv::Laplacian(focus1, focus2, CV_64F, 3, 1, 0);
            cv::Scalar mean;
            cv::Scalar stddev;
            cv::meanStdDev(focus2, mean, stddev);
            auto blurriness = 1000.0 / stddev[0];
            LOG("blurriness: "<<blurriness);*/


            /** adjust BGR colors **/
            auto ptr = (std::uint16_t *) cam_rgb48.data;
            for (int y = 0; y < ht; ++y) {
                for (int x = 0; x < wd; ++x) {
                    int r0 = ptr[2];
                    int g0 = ptr[1];
                    int b0 = ptr[0];
                    /** subtract black. **/
                    r0 -= 871;
                    g0 -= 1259;
                    b0 -= 1799;
                    /** convert observed rgb to srgb using best guess matrix. **/
                    int r1 = (100*r0 -  16*g0 -   0*b0)/100;
                    int g1 = (-22*r0 +  63*g0 -   8*b0)/100;
                    int b1 = (  2*r0 -  33*g0 +  83*b0)/100;
                    r1 = std::max(std::min(r1, 65535),0);
                    g1 = std::max(std::min(g1, 65535),0);
                    b1 = std::max(std::min(b1, 65535),0);
                    ptr[2] = r1;
                    ptr[1] = g1;
                    ptr[0] = b1;
                    ptr += 3;
                }
            }
            /*ptr += 3 * (wd/2 + wd*ht/2);
            int r = ptr[2];
            int g = ptr[1];
            int b = ptr[0];
            LOG("rgb= "<<r<<" "<<g<<" "<<b);*/

            /** show it. **/
            cv::imshow(win_name, cam_rgb48);

            /** wait for user input. **/
            int key = cv::waitKey(30);
            if (key >= 0) {
                LOG("key="<<key);
                break;
            }
        }

        /** clean up and go home. **/
        cv::destroyWindow(win_name);
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
