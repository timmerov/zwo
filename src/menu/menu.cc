/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
run the menu thread.

this thread produces most of the data in the settings buffer.
there are a small number of exceptions.
we assume all other threads do not modify anything other than strings.
we are a bit lazy when it comes to looking at the data.
if we are the producer then we can take a shortcut:

    x = settings->x;
    x = new_x;
    {
        lock_guard lock;
        settings->x = x;
    }

we always use the lock when changing the settings for consistency.
even when we don't need to like in the example where updating x is atomic.
obviously we cannot take this shortcut when
looking at data that might be modified by other threads.

exceptions:
    input
    right ascension, declination
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
        SettingsBuffer *settings
    ) noexcept : agm::Thread("MenuThread") {
        settings_ = settings;
    }

    virtual ~MenuThread() noexcept {
        delete mount_;
    }

    virtual void begin() noexcept {
        LOG("MenuThread");
        mount_ = Ioptron::create(settings_);
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

        case 'd':
            toggleSubtractMedian();
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
            handleStarList();
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
            /** add a trailing end line. **/
            input_ += '\n';
        } else {
            /** include the end of line. **/
            ++pos;
            /** copy the first line to line. **/
            input_ = input_lines_.substr(0, pos);
            /** erase the first line from input. **/
            input_lines_.erase(0, pos);
        }

        /** remove trailing comments after // **/
        pos = input_.find("//");
        if (pos != std::string::npos) {
            input_.erase(pos);
            /** restore the trailing end line. **/
            input_ += '\n';
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
        ch = std::tolower(ch);
        return ch;
    }

    double popAngleFromInput() noexcept {
        /** load the stringstream with the input. **/
        std::stringstream ss;
        ss<<input_;

        std::string valid_units("d*'\"hms");
        bool is_first = true;
        double total_angle = 0.0;
        int erase_count = 0;

        /** combine angle with different units. **/
        for(;;) {
            /** pop an angle. **/
            double angle = 0.0;
            ss>>angle;
            //LOG("angle="<<angle);
            if (ss.good() == false) {
                break;
            }

            /** pop optional units. **/
            int undo_units_pos = ss.tellg();
            //LOG("undo_units_pos="<<undo_units_pos);
            char units = 'd';
            ss>>units;
            //LOG("units="<<units);

            /** check valid units. **/
            auto found_pos = std::string::npos;
            if (ss.good()) {
                found_pos = valid_units.find(units);
                //LOG("found_pos="<<found_pos);
            }
            if (found_pos == std::string::npos) {
                /** numbers after the first must have explicit units. **/
                if (is_first == false) {
                    /** bad format. do not consume the angle. **/
                    //LOG("bad format. done.");
                    break;
                }
                //LOG("seekg undo_units_pos="<<undo_units_pos);
                /**
                the first number defaults to degrees.
                restore whatever it was that wasn't units.
                **/
                ss.seekg(undo_units_pos);
                units = 'd';

                /** we have consumed only the angle. **/
                erase_count = undo_units_pos;
                //LOG("erase_count=undo_units_pos="<<erase_count);
            } else {
                /** we have consumed the angle and the units. **/
                erase_count = ss.tellg();
                //LOG("erase_count=tellg="<<erase_count);
            }

            /** convert the angle to the units. **/
            switch (units) {
            case 'd':
            case '*':
            default:
                /** already in degrees. **/
                valid_units = "'\"";
                break;

            case '\'':
                /** convert to arcminutes. **/
                angle /= 60.0;
                valid_units = "\"";
                break;

            case '"':
                /** convert to arcseconds. **/
                angle /= 60.0 * 60.0;
                valid_units = "";
                break;

            case 'h':
                /** convert to hours. **/
                angle *= 15.0;
                valid_units = "ms";
                break;

            case 'm':
                /** convert to minutes of arc. **/
                angle = angle * 15.0 / 60.0;
                valid_units = "s";
                break;

            case 's':
                /** convert to seconds of arc. **/
                angle = angle * 15.0 / 60.0 / 60.0;
                valid_units = "";
                break;
            }

            /** accumulate. **/
            total_angle += angle;
            //LOG("total_angle="<<total_angle);

            /** stop when we hit arcseconds or seconds. **/
            if (units == '"' || units == 's') {
                break;
            }

            is_first = false;
            //LOG("is_first=false");
        }

        /** erase the consumed characters from the input. **/
        input_.erase(0, erase_count);

        /** return nan if there was no parsable input. **/
        if (is_first) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        return total_angle;
    }

    void showMenu() noexcept {
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);

            LOG("Menu (not case sensitive unless specified):");
            LOG("  a [+-01yn]   : stack (accumulate) images: "<<settings_->accumulate_);
            LOG("  b [+-01yn]   : toggle capture black: "<<settings_->capture_black_);
            LOG("  c red blue   : set color balance: r="<<settings_->balance_red_<<" b="<<settings_->balance_blue_);
            LOG("  d [+-01yn]   : toggle subtract median: "<<settings_->subtract_median_);
            LOG("  e [+-01yn]   : toggle auto exposure: "<<settings_->auto_exposure_);
            LOG("  e usecs      : set exposure microseconds (disables auto): "<<settings_->exposure_);
            LOG("  f [+-01yn]   : toggle manual focus helper: "<<settings_->show_focus_);
            LOG("  g pwr        : set gamma (1.0): "<<settings_->gamma_);
            LOG("  i [+-01yn]   : toggle auto iso linear scaling: "<<settings_->auto_iso_);
            LOG("  i iso        : set iso linear scaling [100 none] (disables auto): "<<settings_->iso_);
            LOG("  k [+-01yn]   : toggle collimation circles: "<<settings_->show_circles_);
            LOG("  k x y        : draw collimation circles at x,y: "<<settings_->circles_x_<<","<<settings_->circles_y_);
            LOG("  l file       : load image file");
            LOG("  ma [nsew] x  : slew n,s,e,w by angle DD[d*] MM' SS.SS\" or HHh MMm SS.SSs");
            LOG("  mi           : show mount info");
            LOG("  mg ra dec    : goto this position");
            LOG("  mh           : slew to home (zero) position");
            LOG("  mm [nsew] ms : slew n,s,e,w for milliseconds (time)");
            LOG("  mr#          : set slewing rate 1-9");
            LOG("  mt [+-01yn]  : toggle tracking: "<<is_tracking_);
            LOG("  p path       : prefix for saved files: "<<settings_->save_path_);
            LOG("  q,esc        : quit");
            LOG("  r [+-01yn]   : toggle fps (frame Rate): "<<settings_->show_fps_);
            LOG("  s file       : save the displayed image (disables stacking).");
            LOG("  t file       : save the raw 16 bit image as tiff.");
            LOG("  t file#      : save a sequence of 16 bit tiffs where # is replaced by a number.");
            LOG("  t [+-01yn]   : stop or resume saving 16 bit tiffs: "<<settings_->auto_save_);
            LOG("  x            : run the experiment of the day");
            LOG("  z [+-01yn]   : find and circle stars: "<<settings_->find_stars_);
            LOG("  zb           : begin new star list.");
            LOG("  zc           : calculate center from star lists.");
            LOG("  zd [x]       : delete star list x or all star lists.");
            LOG("  ze           : end star list.");
            LOG("  zl           : show star lists.");
            LOG("  zs file      : save star lists.");
            LOG("  ?            : show help");
        }
    }

    void toggleAccumulate() noexcept {
        bool new_accumulate = settings_->accumulate_;
        toggleOnOff(new_accumulate);
        LOG("MenuThread stack (accumulate) images: "<<new_accumulate);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->accumulate_ = new_accumulate;
        }
    }

    void toggleCaptureBlack() noexcept {
        bool new_capture_black = settings_->capture_black_;
        toggleOnOff(new_capture_black);
        LOG("MenuThread capture black: "<<new_capture_black);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->capture_black_ = new_capture_black;
        }
    }

    void setColorBalance() noexcept {
        std::stringstream ss;
        ss << input_;
        double new_balance_red = settings_->balance_red_;
        double new_balance_blue = settings_->balance_blue_;
        ss >> new_balance_red >> new_balance_blue;
        LOG("ManeuThread color balance: r="<<new_balance_red<<" b="<<new_balance_blue);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->balance_red_ = new_balance_red;
            settings_->balance_blue_ = new_balance_blue;
        }
    }

    void toggleSubtractMedian() noexcept {
        bool new_subtract_median = settings_->subtract_median_;
        toggleOnOff(new_subtract_median);
        LOG("MenuThread subtract median: "<<new_subtract_median);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->subtract_median_ = new_subtract_median;
        }
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
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->auto_exposure_ = new_auto_exposure;
            settings_->exposure_ = new_exposure;
        }
    }

    void toggleFocus() noexcept {
        bool new_focus = settings_->show_focus_;
        toggleOnOff(new_focus);
        LOG("MenuThread focus: "<<new_focus);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->show_focus_ = new_focus;
        }
    }

    void toggleGamma() noexcept {
        double new_gamma = getDouble(1.0);
        if (new_gamma <= 0.0) {
            new_gamma = 1.0;
        }

        LOG("MenuThread gamma: "<<new_gamma);
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->gamma_ = new_gamma;
        }
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
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->show_fps_ = new_fps;
        }
    }

    void handleMount() noexcept {
        int ch = popCommandFromInput();
        switch (ch) {
        case 'a':
            slewAngle();
            break;

        case 'g':
            slewToPosition();
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

        default:
            LOG("Unknown command for mount.");
            break;
        }
    }

    void slewAngle() noexcept {
        /** which direction. **/
        int dir = popCommandFromInput();
        dir = std::tolower(dir);
        auto pos = std::string("nsew").find(dir);
        if (pos == std::string::npos) {
            LOG("MenuThread Slew direction must be one of n,s,e,w.");
            return;
        }

        /** how far. **/
        double angle = popAngleFromInput();
        if (std::isnan(angle)) {
            LOG("MenuThread angle format is invalid.");
            return;
        }
        if (angle == 0.0) {
            LOG("MenuThread Slew angle is zero.");
            return;
        }

        /** convert degrees to arcseconds. **/
        double arcseconds = angle * 60 * 60;

        /** slew **/
        mount_->moveArcseconds(dir, arcseconds);
        is_tracking_ = true;
    }

    void slewToPosition() noexcept {
        bool good = true;
        double ra = popAngleFromInput();
        double dec = popAngleFromInput();
        if (std::isnan(ra)) {
            LOG("MenuThread Invalid format for right ascension.");
            good = false;
        }
        if (std::isnan(dec)) {
            LOG("MenuThread Invalid format for declination.");
            good = false;
        }
        if (good == false) {
            return;
        }
        mount_->goToPosition(ra, dec);
    }

    void slewDuration() noexcept {
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
        /** stop ourselves immediately. **/
        stop();
    }

    void loadImage() noexcept {
        auto filename = getString();
        if (filename.size() == 0) {
            return;
        }

        LOG("MenuThread load file: "<<filename);

        /** wait for the capture thread to consume the previous image. **/
        while (settings_->load_file_name_.size() > 0) {
            agm::sleep::milliseconds(10);
        }

        /** pass the filename to the capture thread. **/
        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->load_file_name_ = std::move(filename);
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
            settings_->save_path_ = std::move(path);
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
            settings_->save_file_name_ = std::move(filename);
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
            settings_->auto_save_ = new_auto_save;
            settings_->raw_file_name_ = std::move(new_filename);
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

    void handleStarList() noexcept {

        /** wait for the window thread to consume the previous star command. **/
        while (settings_->star_command_ != StarCommand::kNone) {
            agm::sleep::milliseconds(10);
        }

        bool new_find_stars = settings_->find_stars_;
        auto star_command = StarCommand::kNone;
        int star_param = 0;
        std::string star_file_name;

        /** simple toggle. **/
        bool set = toggleOnOff(new_find_stars);
        if (set) {
            LOG("MenuThread find stars: "<<new_find_stars);
        } else {
            /** complex command. **/
            int ch = popCommandFromInput();
            switch (ch) {
            case 'b':
                LOG("MenuThread star command: begin list");
                star_command = StarCommand::kBegin;
                break;

            case 'c':
                LOG("MenuThread star command: calculate center");
                star_command = StarCommand::kCalculateCenter;
                break;

            case 'd': {
                std::stringstream ss;
                ss << input_;
                star_param = -1;
                ss >> star_param;
                if ( star_param < 0) {
                    LOG("MenuThread star command: delete all lists");
                    star_command = StarCommand::kDeleteAll;
                } else {
                    LOG("MenuThread star command: delete list["<<star_param<<"]");
                    star_command = StarCommand::kDelete;
                }
            } break;

            case 'e':
                LOG("MenuThread star command: end list");
                star_command = StarCommand::kEnd;
                break;

            case 'l':
                LOG("MenuThread star command: show lists");
                star_command = StarCommand::kList;
                break;

            case 'q': {
                std::stringstream ss;
                ss <<input_;
                std::string filename;
                ss >> filename;
                LOG("MenuThread star command: generate quads");
                star_command = StarCommand::kQuads;
                star_file_name = std::move(filename);
            } break;

            case 's': {
                std::stringstream ss;
                ss <<input_;
                std::string filename;
                ss >> filename;
                if (filename.size() > 0) {
                    LOG("MenuThread star command: save to \""<<filename<<"\"");
                    star_command = StarCommand::kSave;
                    star_file_name = std::move(filename);
                } else {
                    LOG("MensuThread save star list command missing filename.");
                }
            } break;

            default:
                LOG("MenuThread invalid star command: '"<<(char)ch<<"'");
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lock(settings_->mutex_);
            settings_->find_stars_ = new_find_stars;
            settings_->star_command_ = star_command;
            settings_->star_param_ = star_param;
            settings_->star_file_name_ = std::move(star_file_name);
        }
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
        LOG("-- Balance colors. C r b");
        LOG("-- Stack (accumulate) images. A");
        LOG("-- Wait as long as you wish.");
        LOG("-- Save the image.");
        LOG("-- Profit.");
    }
};
}

agm::Thread *createMenuThread(
    SettingsBuffer *settings
) noexcept {
    return new(std::nothrow) MenuThread(settings);
}
