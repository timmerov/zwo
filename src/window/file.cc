/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
save images to file.

window thread
**/

#include <tiffio.h>

#include "window.h"


namespace WindowThread {

/** save the image to the file. **/
void WindowThread::saveImage() noexcept {
    if (save_file_name_.size()) {
        if (accumulate_) {
            saveAccumulatedImage();
        } else {
            saveDisplayImage();
        }
    } else if (raw_file_name_.size()) {
        saveRawImage();
    }
}

/** save the 8 bit gamma corrected image. **/
void WindowThread::saveDisplayImage() noexcept {
    bool success = saveImage8();
    if (success) {
        LOG("CaptureThread Saved gamma corrected 8 bit image to file: "<<save_file_name_);
    }
}

/** save the 16 bit raw image. **/
void WindowThread::saveRawImage() noexcept {
    bool success = saveImage16();
    if (success) {
        LOG("CaptureThread Saved raw image to 16 bit tiff file: "<<raw_file_name_);
    }
}

/** save the accumulated image and disable stacking. **/
void WindowThread::saveAccumulatedImage() noexcept {
    bool success = saveImage32();
    if (success == false) {
        return;
    }

    LOG("CaptureThread Saved image to 32 bit tiff file: "<<save_file_name_);

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
    try {
        std::string filename = save_path_ + save_file_name_;
        success = cv::imwrite(filename, rgb8_gamma_);
    } catch (const cv::Exception& ex) {
        LOG("CaptureThread Failed to save image to file: "<<save_file_name_<<" OpenCV reason: "<<ex.what());
    }
    return success;
}

/** save the raw 16 bit image using tiff. **/
bool WindowThread::saveImage16() noexcept {
    /** create the tiff file. **/
    std::string filename = save_path_ + raw_file_name_;
    TIFF *tiff = TIFFOpen(filename.c_str(), "w");
    if (tiff == nullptr) {
        LOG("CaptureThread Failed to create tiff file: "<<raw_file_name_);
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
        LOG("CaptureThread Failed to write tiff file: "<<raw_file_name_);
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
        LOG("CaptureThread Failed to create tiff file: "<<save_file_name_);
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
        LOG("CaptureThread Failed to write tiff file: "<<save_file_name_);
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

} // WindowThread
