/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
display images in a window.
**/

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/thread.h>

#include <shared/image_double_buffer.h>
#include <shared/settings_buffer.h>

#include "findstars.h"


namespace WindowThread {

class WindowThread : public agm::Thread, public Settings {
public:
    /** share data with the capture thread. **/
    ImageDoubleBuffer *image_double_buffer_ = nullptr;
    bool resume_waiting_ = false;

    ImageBuffer *img_ = nullptr;
    /** share data with the menu thread. **/
    SettingsBuffer *settings_ = nullptr;

    /** our fields. **/
    cv::String win_name_;
    bool first_image_ = false;
    cv::Mat rgb16_;
    cv::Mat black_;
    cv::Mat rgb32_;
    cv::Mat cropped16_;
    cv::Mat gray16_;
    cv::Mat gray8_;
    cv::Mat laplace_;
    cv::Mat rgb8_gamma_;
    cv::Mat cropped8_;
    double base_stddev_ = 0.0;
    int gamma_max_ = 0;
    agm::uint8 *gamma_table_ = nullptr;
    int *histr_ = nullptr;
    int *histg_ = nullptr;
    int *histb_ = nullptr;
    int nstacked_ = 0;
    agm::int64 fps_start_ = 0;
    int fps_count_ = 0;
    int display_width_ = 0;
    int display_height_ = 0;
    cv::Rect aoi_;
    bool logged_once_ = false;
    cv::Mat median16_;
    std::vector<int> median_hist_;
    int black_frames_ = 0;
    double black_mean_ = 0.0;
    double black_std_dev_ = 0.0;
    std::vector<int> bad_pixels_;
    int auto_save_counter_ = 0;
    std::string auto_save_name_;
    StarData star_;

    WindowThread(
        ImageDoubleBuffer *image_double_buffer,
        SettingsBuffer *settings
    ) noexcept;

    virtual ~WindowThread() = default;

    virtual void begin() noexcept;

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept;

    /**
    we need o call cv::waitKey periodically.
    we also need to wait for a new captured image.
    which could take a long time.
    long enough that the os thinks the program crashed.
    */
    void wait_for_swap() noexcept;

    virtual void end() noexcept;

    void copySettings() noexcept;

    void subtractMedian() noexcept;

    void checkBlurriness() noexcept;

    /** no alignment **/
    void stackImages() noexcept;

    void isoLinearScale() noexcept;

    void gammaPowerScale() noexcept;

    void balanceColors() noexcept;

    void showHistogram() noexcept;

    void plotHistogram(
        int *hist,
        int color
    ) noexcept;

    /** the gamma table maps 16 bit to 8 bit. **/
    void initGammaTable() noexcept;

    /**
    scale the source 16 bit components to the size of the gamma lookup table.
    set the destination 8 bit values.
    **/
    void applyDisplayGamma() noexcept;

    /** draw concentric circles to aid collimation. **/
    void showCollimationCircles() noexcept;

    /** draw a circle given center and radius. **/
    void drawCircle(
        double center_x,
        double center_y,
        double r
    ) noexcept;

    /** draw a circle given center and radius. **/
    void drawCircle(
        int cx,
        int cy,
        int radius
    ) noexcept;

    /** draw 4 or 8 dots. **/
    void draw8Dots(
        int cx,
        int cy,
        int x,
        int y
    ) noexcept;

    /** draw a red blended dot at the location. **/
    void drawDot(
        int x,
        int y
    ) noexcept;

    /** get the size of the default display. **/
    void getDisplayResolution() noexcept;

    /** crop the captured image if necessary. **/
    void setWindowCrop() noexcept;


    /** vvvvv ----- black.cc ----- vvvvv **/

    /**
    capture a sequence of black frames.
    **/
    void captureBlack() noexcept;

    void processBlack() noexcept;

    void fixBadPixels() noexcept;

    /**
    subtract black from the image.
    **/
    void subtractBlack() noexcept;

    /** ^^^^^ ----- black.cc ----- ^^^^^ **/

    /** vvvvv ----- file.cc ----- vvvvv **/

    /** maybe save the raw image. **/
    void saveImageRaw() noexcept;

    /** maybe save the diplayed or stacked image. **/
    void saveImageDisplayStacked() noexcept;

    /** save the 8 bit gamma corrected image. **/
    void saveDisplayImage() noexcept;

    /** save the 16 bit raw image. **/
    void saveRawImage() noexcept;

    /** auto Save the 16 bit raw image. **/
    void autoSaveRawImage() noexcept;

    /** save the accumulated image and disable stacking. **/
    void saveAccumulatedImage() noexcept;

    /** save the 8 bit image using opencv. **/
    bool saveImage8() noexcept;

    /** save the raw 16 bit image using tiff. **/
    bool saveImage16(const std::string& filename) noexcept;

    /** save the 32 bit image using tiff. **/
    bool saveImage32() noexcept;

    int scale32(
        int src,
        int scale
    ) noexcept;

    /** save star information to text file. **/
    void saveStars(const std::string& filename) noexcept;

    /** ^^^^^ ----- file.cc ----- ^^^^^ **/

    /** vvvvv ----- findstars.cc ----- vvvvv **/

    void findStars() noexcept;

    void findStarsInImage() noexcept;

    int countBrightPixels(
        int cx,
        int cy,
        int r,
        int hh
    ) noexcept;

    void blobCentroid(
        StarPosition& star,
        int cx,
        int cy,
        int r
    ) noexcept;

    void eraseBlob(
        int cx,
        int cy,
        int r
    ) noexcept;

    StarPosition *checkCollision(
        StarPosition& star
    ) noexcept;

    void showStars() noexcept;

    void findMedianGrays() noexcept;

    void handleStarCommand() noexcept;

    void beginStarList() noexcept;

    void deleteStarList() noexcept;

    void deleteAllStarLists() noexcept;

    void endStarList() noexcept;

    void showStarLists() noexcept;

    void addStarsToList() noexcept;

    /** ^^^^^ ----- findstars.cc ----- ^^^^^ **/
};

} // WindowThread
