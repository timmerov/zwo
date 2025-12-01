/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
run the menu thread.
**/

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/nonblocking_input.h>
#include <aggiornamento/master.h>
#include <aggiornamento/thread.h>

#include <shared/settings_buffer.h>

#include "ioptron.h"


namespace {
class MenuThread : public agm::Thread {
public:
    agm::NonBlockingInput nbi_;
    SettingsBuffer *settings_ = nullptr;
    std::string input_;
    std::string input_lines_;
    Ioptron *mount_ = nullptr;
    bool is_tracking_ = false;

    MenuThread(
        SettingsBuffer *settings_buffer
    ) noexcept : agm::Thread("MenuThread") {
        settings_ = settings_buffer;
    }

    virtual ~MenuThread() noexcept {
        delete mount_;
    }

    virtual void begin() noexcept {
        LOG("MenuThread");
        mount_ = Ioptron::create();
        mount_->connect();
        loadConfigFile("zwo.cfg");
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        get_input();
        parse_input();
        agm::sleep::milliseconds(10);
    }

    virtual void end() noexcept {
        mount_->disconnect();
        delete mount_;
        mount_ = nullptr;
        LOG("MenuThread disconnected the mount.");
    }

    /** get input from stdin and from the settings buffer. **/
    void get_input() noexcept {
        /** don't get more input if we already have input. **/
        if (input_lines_.size() > 0) {
            return;
        }

        /** get input from stdin. **/
        input_lines_ = nbi_.get();
        if (input_lines_.size()) {
            return;
        }

        /** get input from some other thread. **/
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            /**
            std::move means raid my resources.
            it does not mean clear them.
            **/
            input_lines_ = std::move(settings_->input_);
            settings_->input_.clear();
        }
        if (input_lines_.size()) {
            return;
        }
    }

    void parse_input() noexcept {
        if (input_lines_.size() == 0) {
            return;
        }
        getFirstLine();
        int ch = popCommandFromInput();
        switch (ch) {
        case 'a':
            toggleAccumulate();
            break;

        case 'b':
            toggleCaptureBlack();
            break;

        case 'c':
            setColorBalance();
            break;

        case 'e':
            toggleAutoExposure();
            break;

        case 'f':
            toggleFocus();
            break;

        case 'g':
            toggleGamma();
            break;

        case 'h':
            toggleHistogram();
            break;

        case 'i':
            toggleIso();
            break;

        case 'k':
            showHideCircles();
            break;

        case 'l':
            loadImage();
            break;

        case 'm':
            handleMount();
            break;

        case 'p':
            setSavePath();
            break;

        case 'q':
        case 27: /*escape*/
            quit();
            break;

        case 'r':
            toggleFps();
            break;

        case 's':
            saveImage();
            break;

        case 't':
            saveRaw();
            break;

        case 'x':
            experiment();
            break;

        case 'z':
            toggleFindStars();
            break;

        case '?':
            showHelp();
            break;

        default:
            showMenu();
            break;
        }

        /** consume one line of input. **/
        auto eol = input_.find('\n');
        if (eol == std::string::npos) {
            input_.clear();
        } else {
            input_.erase(0, eol + 1);
        }
    }

    void getFirstLine() noexcept {
        /** find the end of line. **/
        auto pos = input_lines_.find('\n');
        if (pos == std::string::npos) {
            /** move the entire input to line. **/
            input_ = std::move(input_lines_);
            input_lines_.clear();
        } else {
            /** include the end of line. **/
            ++pos;
            /** copy the first line to line. **/
            input_ = input_lines_.substr(0, pos);
            /** erase the first line from input. **/
            input_lines_.erase(0, pos);
        }
    }

    int popCommandFromInput() noexcept {
        int len = input_.size();
        int pos = 0;
        int ch = 0;
        for (; pos < len; ++pos) {
            ch = input_[pos];
            if (ch == '\n') {
                return 0;
            }
            if (std::isspace(ch)) {
                continue;
            }
            break;
        }
        if (pos >= len) {
            return 0;
        }
        input_.erase(0, pos + 1);
        return ch;
    }

    void showMenu() noexcept {
        LOG("Menu (not case sensitive unless specified):");
        LOG("  a [+-01yn]   : stack (accumulate) images: "<<settings_->accumulate_);
        LOG("  b [+-01yn]   : toggle capture black: "<<settings_->capture_black_);
        LOG("  c red blue   : set color balance: r="<<settings_->balance_red_<<" b="<<settings_->balance_blue_);
        LOG("  e [+-01yn]   : toggle auto exposure: "<<settings_->auto_exposure_);
        LOG("  e usecs      : set exposure microseconds (disables auto): "<<settings_->exposure_);
        LOG("  f [+-01yn]   : toggle manual focus helper: "<<settings_->show_focus_);
        LOG("  g pwr        : set gamma (1.0): "<<settings_->gamma_);
        LOG("  h [+-01yn]   : toggle histogram: "<<settings_->show_histogram_);
        LOG("  i [+-01yn]   : toggle auto iso linear scaling: "<<settings_->auto_iso_);
        LOG("  i iso        : set iso linear scaling [100 none] (disables auto): "<<settings_->iso_);
        LOG("  k [+-01yn]   : toggle collimation circles: "<<settings_->show_circles_);
        LOG("  k x y        : draw collimation circles at x,y: "<<settings_->circles_x_<<","<<settings_->circles_y_);
        LOG("  l file       : load image file");
        LOG("  ma [nsew] x  : slew n,s,e,w for arcseconds (angle)");
        LOG("  mi           : show mount info");
        LOG("  mh           : slew to home (zero) position");
        LOG("  mm [nsew] ms : slew n,s,e,w for milliseconds (time)");
        LOG("  mr#          : set slewing rate 1-9");
        LOG("  mt [+-01yn]  : toggle tracking");
        LOG("  mz           : set zero (home) position");
        LOG("  p path       : prefix for saved files: "<<settings_->save_path_);
        LOG("  q,esc        : quit");
        LOG("  r [+-01yn]   : toggle fps (frame Rate): "<<settings_->show_fps_);
        LOG("  s file       : save the image (disables stacking).");
        LOG("  t file       : save the raw 16 bit image as tiff.");
        LOG("  t file#      : save a sequence of 16 bit tiffs where # is replaced by a number.");
        LOG("  t [+-01yn]   : stop or resume saving 16 bit tiffs.");
        LOG("  x            : run the experiment of the day");
        LOG("  z [+-01yn]   : find and circle stars");
        LOG("  ?            : show help");
    }

    void toggleAccumulate() noexcept {
        bool new_accumulate = settings_->accumulate_;
        toggleOnOff(new_accumulate);
        LOG("MenuThread stack (accumulate) images: "<<new_accumulate);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->accumulate_ = new_accumulate;
    }

    void toggleCaptureBlack() noexcept {
        bool new_capture_black = settings_->capture_black_;
        toggleOnOff(new_capture_black);
        LOG("MenuThread capture black: "<<new_capture_black);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->capture_black_ = new_capture_black;
    }

    void setColorBalance() noexcept {
        std::stringstream ss;
        ss << input_;
        double new_balance_red = settings_->balance_red_;
        double new_balance_blue = settings_->balance_blue_;
        ss >> new_balance_red >> new_balance_blue;
        LOG("ManeuThread color balance: r="<<new_balance_red<<" b="<<new_balance_blue);

        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->balance_red_ = new_balance_red;
        settings_->balance_blue_ = new_balance_blue;
    }

    void toggleAutoExposure() noexcept {
        bool new_auto_exposure = settings_->auto_exposure_;
        int new_exposure = settings_->exposure_;

        bool set = toggleOnOff(new_auto_exposure);
        if (set == false) {
            int test_exposure = getInt(new_exposure);
            if (test_exposure > 0) {
                new_exposure = test_exposure;
            }
        }

        LOG("MenuThread auto exposure: "<<new_auto_exposure);
        LOG("MenuThread exposure: "<<new_exposure);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->auto_exposure_ = new_auto_exposure;
        settings_->exposure_ = new_exposure;
    }

    void toggleFocus() noexcept {
        bool new_focus = settings_->show_focus_;
        toggleOnOff(new_focus);
        LOG("MenuThread focus: "<<new_focus);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_focus_ = new_focus;
    }

    void toggleGamma() noexcept {
        double new_gamma = getDouble(1.0);
        if (new_gamma <= 0.0) {
            new_gamma = 1.0;
        }

        LOG("MenuThread gamma: "<<new_gamma);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->gamma_ = new_gamma;
    }

    void toggleHistogram() noexcept {
        bool new_histogram = settings_->show_histogram_;
        toggleOnOff(new_histogram);
        LOG("MenuThread histogram: "<<new_histogram);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_histogram_ = new_histogram;
    }

    void toggleIso() noexcept {
        bool new_auto_iso = settings_->auto_iso_;
        int new_iso = settings_->iso_;

        bool set = toggleOnOff(new_auto_iso);
        if (set == false) {
            int test_iso = getInt(-1);
            if (test_iso > 0) {
                new_iso = test_iso;
                set = true;
            }
        }

        LOG("MenuThread auto iso: "<<new_auto_iso);
        LOG("MenuThread iso: "<<new_iso);
        if (set) {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->auto_iso_ = new_auto_iso;
            settings_->iso_ = new_iso;
        }
    }

    void showHideCircles() noexcept {
        bool new_circles = settings_->show_circles_;
        bool new_x = settings_->circles_x_;
        bool new_y = settings_->circles_y_;

        bool set = toggleOnOff(new_circles);
        if (set == false) {
            std::stringstream ss;
            ss << input_;
            double x = -2.0;
            double y = -2.0;
            ss >> x >> y;
            if (x >= -1.0 && x <= +1.0 && y >= -1.0 && y <= +1.0) {
                new_circles = true;
                new_x = x;
                new_y = y;
                set = true;
            }
        }

        LOG("MenuThread show collimation circles: "<<new_circles<<" "<<new_x<<","<<new_y);
        if (set) {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->show_circles_ = new_circles;
            settings_->circles_x_ = new_x;
            settings_->circles_y_ = new_y;
        }
    }

    void toggleFps() noexcept {
        bool new_fps = settings_->show_fps_;
        toggleOnOff(new_fps);
        LOG("MenuThread show fps (frame rate): "<<new_fps);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_fps_ = new_fps;
    }

    void handleMount() noexcept {
        int ch = popCommandFromInput();
        switch (ch) {
        case 'a':
            slewArcseconds();
            break;

        case 'h':
            mount_->slewToHomePosition();
            break;

        case 'i':
            mount_->showStatus();
            break;

        case 'm':
            slewDuration();
            break;

        case 'r': {
            int rate = input_[1] - '0';
            mount_->setSlewingRate(rate);
        } break;

        case 't': {
            toggleOnOff(is_tracking_);
            LOG("MenuThread tracking: "<<is_tracking_);
            mount_->setTracking(is_tracking_);
        } break;

        case 'z':
            mount_->setZeroPosition();
            break;

        default:
            LOG("Unknown command for mount.");
            break;
        }
    }

    void slewArcseconds() {
        std::stringstream ss;
        ss << input_;
        char dir = 0;
        double arcseconds = 0.0;
        ss >> dir >> arcseconds;
        dir = std::tolower(dir);
        auto pos = std::string("nsew").find(dir);
        if (pos == std::string::npos) {
            LOG("MenuThread Slew direction must be one of n,s,e,w.");
            return;
        }
        if (arcseconds == 0.0) {
            LOG("MenuThread Slew arcseconds is zero.");
            return;
        }
        mount_->moveArcseconds(dir, arcseconds);
    }

    void slewDuration() {
        std::stringstream ss;
        ss << input_;
        char dir = 0;
        double ms = 0.0;
        ss >> dir >> ms;
        dir = std::tolower(dir);
        auto pos = std::string("nsew").find(dir);
        if (pos == std::string::npos) {
            LOG("MenuThread Slew direction must be one of n,s,e,w.");
            return;
        }
        if (ms <= 0.0) {
            LOG("MenuThread Slew duration must be greater than zero.");
            return;
        }
        mount_->moveMilliseconds(dir, ms);
    }

    /** parse the input as a number. **/
    int getInt(
        int default_value
    ) noexcept {
        std::stringstream ss;
        ss << input_;
        int value = default_value;
        ss >> value;
        return value;
    }

    /** parse the input as a number. **/
    double getDouble(
        double default_value
    ) noexcept {
        std::stringstream ss;
        ss << input_;
        double value = default_value;
        ss >> value;
        return value;
    }

    /** parse the input as a string. **/
    std::string getString() noexcept {
        std::stringstream ss;
        ss <<input_;
        std::string str;
        ss >> str;
        return str;
    }

    /**
    start at the second character of the input.
    look for plus/minus 0/1 y/n.
    which means on/off.
    if there is nothing then invert the input.
    and return true.
    otherwise leave it alone and return false.
    **/
    bool toggleOnOff(
        bool &cur_value
    ) noexcept {
        std::stringstream ss;
        ss << input_;
        std::string str;
        std::string rem;
        ss >> str >> rem;
        if (str == "") {
            /** flip it. **/
            cur_value = !cur_value;
            return true;
        }
        if (str.size() > 1 || rem.size() > 0) {
            /** leave it alone. **/
            return false;
        }
        int ch = str[0];
        switch (ch) {
            case '-':
            case '0':
            case 'n':
            case 'N':
                /** toggle off. **/
                cur_value = false;
                return true;

            case '+':
            case '1':
            case 'y':
            case 'Y':
                /** toggle on. **/
                cur_value = true;
                return true;
        }
        /** leave it alone. **/
        return false;
    }

    /** stop all threads. **/
    void quit() noexcept {
        LOG("MenuThread stopping all threads.");
        agm::master::setDone();
    }

    void loadImage() noexcept {
        auto filename = getString();
        if (filename.size() == 0) {
            return;
        }

        LOG("MenuThread load file: "<<filename);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            std::swap(settings_->load_file_name_, filename);
        }
    }

    void setSavePath() noexcept {
        auto path = getString();

        /** ensure there's a trailing slash. **/
        if (path.size() > 0 && path.back() != '/') {
            path += '/';
        }

        LOG("MenuThread save path: "<<path);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            std::swap(settings_->save_path_, path);
        }
    }

    void saveImage() noexcept {
        auto filename = getString();
        if (filename.size() == 0) {
            return;
        }

        LOG("MenuThread save file: "<<filename);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            std::swap(settings_->save_file_name_, filename);
        }
    }

    void saveRaw() noexcept {
        bool new_auto_save = settings_->auto_save_;
        std::string new_filename;

        bool set = toggleOnOff(new_auto_save);
        if (set == false) {
            new_filename = getString();
            auto found = new_filename.find('#');
            if (found != std::string::npos) {
                new_auto_save = true;
            }
        }

        LOG("MenuThread auto save: "<<new_auto_save);
        LOG("MenuThread save raw: "<<new_filename);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            std::swap(settings_->auto_save_, new_auto_save);
            std::swap(settings_->raw_file_name_, new_filename);
        }
    }

    /** stuff the config file into the input buffer. **/
    void loadConfigFile(
        const char *filename
    ) noexcept {
        std::ifstream cfg(filename);
        if (cfg.is_open()) {
            LOG("MenuThread Loading commands from config file \""<<filename<<"\".");
            std::stringstream ss;
            ss << cfg.rdbuf();
            std::string input = ss.str();

            if (input.size() > 0) {
                /** change carriage returns to end lines. **/
                std::replace(input.begin(), input.end(), '\r', '\n');

                /** ensure the last character is \n. **/
                if (input.back() != '\n') {
                    input += "\n";
                }

                /** append the lines to the input. **/
                input_lines_ += input;
            }
        } else {
            LOG("MenuThead Config file \""<<filename<<"\" not found.");
        }
    }

    /** run the experiment of the day. **/
    void experiment() noexcept {
        LOG("Experiment of the day.");
        loadConfigFile("experiment.cfg");
    }

    void toggleFindStars() noexcept {
        bool new_find_stars = settings_->find_stars_;
        toggleOnOff(new_find_stars);
        LOG("MenuThread find stars: "<<new_find_stars);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->find_stars_ = new_find_stars;
    }

    /** show how the program is to be used. **/
    void showHelp() noexcept {
        LOG("General usage:");
        LOG("-- Aim the camera at the target.");
        LOG("-- Rough focus the camera.");
        LOG("-- Wait for auto exposure to settle.");
        LOG("-- Disable auto exposure. E");
        LOG("-- Enable manual focus helper. F");
        LOG("-- Minimize the blurriness number.");
        LOG("-- Disable manual focus helper. F");
        LOG("-- Put lens cap on camera.");
        LOG("-- Enable capture black. B");
        LOG("-- Wait for black levels to settle.");
        LOG("-- Disable capture black. B");
        LOG("-- Remove lens cap from camera.");
        LOG("-- Enable histogram. H");
        LOG("-- Balance colors. C r b");
        LOG("-- Disable histogram. H");
        LOG("-- Stack (accumulate) images. A");
        LOG("-- Wait as long as you wish.");
        LOG("-- Save the image.");
        LOG("-- Profit.");
    }
};
}

agm::Thread *createMenuThread(SettingsBuffer *settings_buffer) noexcept {
    return new(std::nothrow) MenuThread(settings_buffer);
}
