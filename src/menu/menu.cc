/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
run the menu thread.
**/

#include <cmath>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/nonblocking_input.h>
#include <aggiornamento/master.h>
#include <aggiornamento/thread.h>

#include <shared/settings_buffer.h>


namespace {
class MenuThread : public agm::Thread {
public:
    agm::NonBlockingInput nbi_;
    SettingsBuffer *settings_ = nullptr;
    std::string input_;

    MenuThread(
        SettingsBuffer *settings_buffer
    ) noexcept : agm::Thread("MenuThread") {
        settings_ = settings_buffer;
    }
    virtual ~MenuThread() = default;

    virtual void begin() noexcept {
        LOG("MenuThread");
    }

    /** run until we're told to stop. **/
    virtual void runOnce() noexcept {
        input_ = nbi_.get();
        parse_input();
        agm::sleep::milliseconds(100);
    }

    virtual void end() noexcept {
        LOG("MenuThread");
    }

    void parse_input() noexcept {
        if (input_.size() == 0) {
            return;
        }
        switch (input_[0]) {
        case 'f':
        case 'F':
            toggleFocus();
            break;

        case 'q':
        case 'Q':
        case 27: /*escape*/
            quit();
            break;

        case 'x':
        case 'X':
            experiment();
            break;

        default:
            showMenu();
            break;
        }
    }

    void showMenu() noexcept {
        LOG("Menu:");
        LOG("  F,f: toggle manual focus helper: "<<settings_->show_focus_);
        LOG("  Q,q,esc: quit ");
        LOG("  X,x: run the experiment of the day");
    }

    void toggleFocus() noexcept {
        auto new_focus = !settings_->show_focus_;
        LOG("MenuThread focus: "<<new_focus);
        std::lock_guard<std::mutex> lock(settings_->mutex_);
        settings_->show_focus_ = new_focus;
    }

    void quit() noexcept {
        /** stop all threads. **/
        LOG("MenuThread stopping all threads.");
        agm::master::setDone();
    }

    void experiment() noexcept {
#if 0
/** derived from dcraw. **/
void display_gamma_curve(
    std::vector<int> &curve,
    double pwr = 1.0/2.222,
    double ts = 4.5,
    int imax = 0x10000
){
    curve.resize(0x10000);

    #define SQR(x) ((x)*(x))
    double g2 = 0.0;
    double g3 = 0.0;
    double g4 = 0.0;

    double bnd[2] = {0.0, 0.0};
    double r;

    pwr = pwr;
    ts = ts;
    g2 = g3 = g4 = 0;
    bnd[ts >= 1] = 1;
    if ((ts-1)*(pwr-1) <= 0) {
        for (int i = 0; i < 48; ++i) {
            g2 = (bnd[0] + bnd[1])/2;
            bnd[(std::pow(g2/ts,-pwr) - 1)/pwr - 1/g2 > -1] = g2;
        }
        g3 = g2 / ts;
        g4 = g2 * (1/pwr - 1);
    }
    for (int i = 0; i < 0x10000; ++i) {
        curve[i] = 0xffff;
        r = (double) i / imax;
        if (r < 1) {
            curve[i] = 0x10000 * (r < g3 ? r*ts : std::pow(r,pwr)*(1+g4)-g4);
        }
    }
}
#endif
        /**
        examine the gamma table.
        we must do this to convert linear rgb to what's shown on the displays.
        the idea is we're going to capture in 16 bit.
        yeah, i know it's not really 16 bit.
        but that's what opencv likes.
        and we're going to stack many images into a 32 bit image.
        the usual practice is to use a look up table.
        but we can't make a 32 bit lookup table.
        so we're going to squash the range of values.
        let's say the max is 120,000.
        we'll scale that to 539.
        then look up the 8 bit value in the table.
        why 539?
        because a lookup table with 540 values is the smallest table size
        that is a multiple of 4 and will span the entire range of 8 bit values
        with no holes.
        **/
        char lut[256];
        for (int i = 0; i < 256; ++i) {
            lut[i] = 0;
        }

        double gamma = 2.22222;
        double pwr = 1.0 / gamma;
        double ts = 4.5;
#if 0
        double bnd[2] = {0.0, 0.0};
        bnd[ts >= 1] = 1;

        double g2 = 0.0;
        double g3 = 0.0;
        double g4 = 0.0;
        if ((ts-1)*(pwr-1) <= 0) {
            for (int i = 0; i < 48; ++i) {
                g2 = (bnd[0] + bnd[1])/2;
                bnd[(std::pow(g2/ts,-pwr) - 1)/pwr - 1/g2 > -1] = g2;
            }
            g3 = g2 / ts;
            g4 = g2 * (1/pwr - 1);
        }
        LOG("pwr="<<pwr);
        LOG("bnd="<<bnd[0]<<" "<<bnd[1]);
        LOG("g2="<<g2<<" g3="<<g3<<" g4="<<g4);
#endif

        double g3 = 0.0180539;
        double g4 = 0.0992964;

        static const int kLutMax = 1124;
        for (int i = 0; i <= kLutMax; ++i) {
            double r = double(i) / double(kLutMax);
            double x;
            if (r < g3) {
                x = r*ts;
            } else {
                x = std::pow(r, pwr)*(1 + g4) - g4;
            }
            x *= 255.0;
            int ix = std::round(x);
            ix = std::max(0, std::min(ix, 255));
            //LOG(i<<": "<<ix);
            lut[ix] = 1;
        }

        for (int i = 0; i < 256; ++i) {
            if (lut[i] == 0) {
                LOG("lut["<<i<<"]=0");
            }
        }
        LOG("done");

#if 0
        for (int i = 0; i <= sz; ++i) {
            double x = std::pow(double(i)/double(sz), gamma) * 255.0;
            int ix = std::round(x);
            ix = std::max(0, std::min(ix, 255));
            LOG(i<<": "<<ix);
            lut[ix] = 1;
        }
#endif
    }
};
}

agm::Thread *createMenuThread(SettingsBuffer *settings_buffer) noexcept {
    return new(std::nothrow) MenuThread(settings_buffer);
}
