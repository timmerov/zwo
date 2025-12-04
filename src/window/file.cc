/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
save images to file.

window thread
**/

#include <fstream>

#include <tiffio.h>

#include "window.h"


namespace WindowThread {

/**
save or auto save the raw image
if a filename was given.
**/
void WindowThread::saveImageRaw() noexcept {
    if (raw_file_name_.size()) {
        saveRawImage();
    }
    if (auto_save_) {
        autoSaveRawImage();
    }
}

/**
save the displayed image or the stacked image
if a filename was given.
**/
void WindowThread::saveImageDisplayStacked() noexcept {
    /** no filename specified. **/
    if (save_file_name_.size() == 0) {
        return;
    }

    /**
    if we're stacking, save that.
    otherwise save the displayed image.
    yeah, it's not the best system.
    **/
    if (accumulate_) {
        saveAccumulatedImage();
    } else {
        saveDisplayImage();
    }
}

/** save the 8 bit gamma corrected image. **/
void WindowThread::saveDisplayImage() noexcept {
    bool success = saveImage8();
    if (success) {
        LOG("WindowThread Saved gamma corrected 8 bit image to file: "<<save_file_name_);
    }
}

/** auto save the 16 bit raw image. **/
void WindowThread::autoSaveRawImage() noexcept {
    /** do we have a new auto save file name? **/
    if (raw_file_name_.size()) {
        auto pos = raw_file_name_.find('#');
        if (pos != std::string::npos) {
            auto_save_name_ = std::move(raw_file_name_);
        }
        raw_file_name_.clear();
    }

    /** do we have an auto save file name? **/
    if (auto_save_name_.size() == 0) {
        return;
    }

    /** do we have a valid auto save file name? **/
    auto pos = auto_save_name_.find('#');
    if (pos == std::string::npos) {
        return;
    }
    std::string prefix = auto_save_name_.substr(0, pos);
    std::string suffix = auto_save_name_.substr(pos + 1);
    std::stringstream ss;
    ss << prefix << std::setw(4) << std::setfill('0') << auto_save_counter_ << suffix;
    std::string filename = ss.str();
    ss.str().clear();
    ss.clear();
    ++auto_save_counter_;

    bool success = saveImage16(filename);
    if (success) {
        LOG("WindowThread Auto saved raw image to 16 bit tiff file: "<<filename);
        saveStars(filename);
    }
}

/** save the 16 bit raw image. **/
void WindowThread::saveRawImage() noexcept {
    bool success = saveImage16(raw_file_name_);
    if (success) {
        LOG("WindowThread Saved raw image to 16 bit tiff file: "<<raw_file_name_);
        saveStars(raw_file_name_);
    }
}

/** save the accumulated image and disable stacking. **/
void WindowThread::saveAccumulatedImage() noexcept {
    bool success = saveImage32();
    if (success == false) {
        return;
    }

    LOG("WindowThread Saved image to 32 bit tiff file: "<<save_file_name_);

    /** disable stacking. **/
    accumulate_ = 0;
    nstacked_ = 0;
    rgb32_ = 0;

    std::lock_guard<std::mutex> lock(settings_->mutex_);
    settings_->accumulate_ = false;
}

/** save the 8 bit image using opencv. **/
bool WindowThread::saveImage8() noexcept {
    /** this is why you do not throw exceptions ever. **/\
    bool success = false;
    std::string filename = save_path_ + save_file_name_;
    try {
        success = cv::imwrite(filename, rgb8_gamma_);
    } catch (const cv::Exception& ex) {
        LOG("WindowThread Failed to save image to file: "<<filename<<" OpenCV reason: "<<ex.what());
    }
    return success;
}

/** save the raw 16 bit image using tiff. **/
bool WindowThread::saveImage16(
    const std::string& raw_file_name
) noexcept {
    /** create the tiff file. **/
    std::string filename = save_path_ + raw_file_name;
    LOG("filename="<<filename);
    TIFF *tiff = TIFFOpen(filename.c_str(), "w");
    if (tiff == nullptr) {
        LOG("WindowThread Failed to create tiff file: "<<filename);
        return false;
    }

    /** do some tiff things. **/
    int wd = img_->width_;
    int ht = img_->height_;
    LOG("wd="<<wd<<" ht="<<ht);
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, wd);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, ht);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);

    /** after we do the above. **/
    int default_strip_size = TIFFDefaultStripSize(tiff, 3 * wd);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, default_strip_size);

    /** allocate a tiff-sized scanline. **/
    int scanline_size = TIFFScanlineSize(tiff);
    auto buffer = new(std::nothrow) char[scanline_size];

    /** zero the trailing bytes. **/
    int src_sz = 3 * sizeof(agm::int16) * wd;
    for (int i = src_sz; i < scanline_size; ++i) {
        buffer[i] = 0;
    }

    /** write all scanlines. **/
    bool success = true;
    auto src = (agm::int16 *) rgb16_.data;
    for (int y = 0; y < ht; ++y) {
        auto dst = (agm::int16 *) buffer;
        for (int x = 0; x < wd; ++x) {
            /** convert opencv BGR to tiff RGB. **/
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            src += 3;
            dst += 3;
        }
        int result = TIFFWriteScanline(tiff, buffer, y, 0);
        if (result < 0) {
            success = false;
            break;
        }
    }

    if (success == false) {
        LOG("WindowThread Failed to write tiff file: "<<filename);
    }

    delete[] buffer;
    TIFFClose(tiff);

    return success;
}

/** save the 32 bit image using tiff. **/
bool WindowThread::saveImage32() noexcept {
    /** create the tiff file. **/
    std::string filename = save_path_ + save_file_name_;
    TIFF *tiff = TIFFOpen(filename.c_str(), "w");
    if (tiff == nullptr) {
        LOG("WindowThread Failed to create tiff file: "<<filename);
        return false;
    }

    /** do some tiff things. **/
    int wd = img_->width_;
    int ht = img_->height_;
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, wd);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, ht);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 32);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

    /** after we do the above. **/
    int default_strip_size = TIFFDefaultStripSize(tiff, 3 * wd);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, default_strip_size);

    /** allocate a tiff-sized scanline. **/
    int scanline_size = TIFFScanlineSize(tiff);
    auto buffer = new(std::nothrow) char[scanline_size];

    /** zero the trailing bytes. **/
    int src_sz = 3 * sizeof(agm::int32) * wd;
    for (int i = src_sz; i < scanline_size; ++i) {
        buffer[i] = 0;
    }

    /** find the max for scaling. **/
    int scale = 0;
    auto src = (agm::int32 *) rgb32_.data;
    int sz = 3 * ht * wd;
    for (int i = 0; i < sz; ++i) {
        int val = *src++;
        scale = std::max(scale, val);
    }

    /** write all scanlines. **/
    bool success = true;
    src = (agm::int32 *) rgb32_.data;
    for (int y = 0; y < ht; ++y) {
        auto dst = (agm::int32 *) buffer;
        for (int x = 0; x < wd; ++x) {
            /** convert opencv BGR to tiff RGB. **/
            dst[0] = scale32(src[2], scale);
            dst[1] = scale32(src[1], scale);
            dst[2] = scale32(src[0], scale);
            src += 3;
            dst += 3;
        }
        int result = TIFFWriteScanline(tiff, buffer, y, 0);
        if (result < 0) {
            success = false;
            break;
        }
    }

    if (success == false) {
        LOG("WindowThread Failed to write tiff file: "<<filename);
    }

    delete[] buffer;
    TIFFClose(tiff);

    return success;
}

int WindowThread::scale32(
    int src,
    int scale
) noexcept {
    static const int kInt32Max = 0x7FFFFFFF;
    agm::int64 x = src;
    x *= kInt32Max;
    x /= scale;
    return (int) x;
}

/** save positions of the stars we found. **/
void WindowThread::saveStars(
    const std::string& filename
) noexcept {
    if (find_stars_ == false) {
        return;
    }
    int nstars = star_positions_.size();
    if (nstars == 0) {
        return;
    }
    auto pos = filename.rfind('.');
    if (pos == std::string::npos) {
        return;
    }

    /** get the ra and dec from the shared buffer. **/
    auto ra = settings_->right_ascension_.toString();
    auto dec = settings_->declination_.toString();

    auto prefix = filename.substr(0, pos);
    auto textname = prefix + ".txt";
    LOG("Writing found star information to file: "<<textname);
    auto pathname = save_path_ + textname;
    std::ofstream fs(pathname);
    fs<<"# Found "<<nstars<<" stars:"<<std::endl;
    fs<<"# x coordinate on screen: left=0 right="<<img_->width_<<std::endl;
    fs<<"# y coordinate on screen: top=0 bottom="<<img_->height_<<std::endl;
    fs<<"# relative brightness: black=0 white=65535"<<std::endl;
    fs<<"# Right ascension: "<<ra<<std::endl;
    fs<<"# Declination: "<<dec<<std::endl;
    fs<<std::endl;
    for (auto&& pos : star_positions_) {
        fs<<pos.x_<<" "<<pos.y_<<" "<<pos.brightness_<<std::endl;
    }
}

} // WindowThread
