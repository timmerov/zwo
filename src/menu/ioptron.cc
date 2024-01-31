/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
drive the ioptron smarteq pro(+) mount.

must issue the :MountInfo# commands.
the :V# and  command is marked as deprecated.
but seems to still be implemented.

some commands of interest:

:AG# retutns the guide rate n.nn x sidereal rate

:RGnnn# set tehs guid rate to nnn*0.01x sidereal rate. nnn is 10 to 80.

:GAS# nnnnnn#
The 1st digit stands for GPS status: 0 means GPS off, 1 means GPS on, 2 means GPS data extracted
correctly.
The 2nd digit stands for system status: 0 means stopped, 1 means tracking with PEC disabled, 2
means slewing, 3 means guiding, 4 means meridian flipping, 5 means tracking with PEC enabled
(only for non-encoder edition), 6 means parked.
The 3rd digit stands for tracking rates: 0 means sidereal rate, 1 means lunar rate, 2 means solar rate,
3 means King rate, 4 means custom rate.
The 4th digit stands for moving speed by arrow button or moving command: 1 means 1x sidereal
tracking rate, 2 means 2x, 3 means 8x, 4 means 16x, 5 means 64x, 6 means 128x, 7 means 256x, 8
means 512x, 9 means maximum speed. Currently, the maximum speed of CEM60 (-EC) is 900x,
the maximum speed of iEQ45 Pro (/AA) is 1400x.
The 5th digit stands for time source: 1 means RS-232 port, 2 means hand controller, 3 means GPS.
The 6th digit stands for hemisphere: 0 means Southern Hemisphere, 1 means Northern Hemisphere.

:GEC# sSSSSSSSSSSSSSSSS
The sign and first 9 digits stands for current Declination, the unit of current Declination is 0.01 arc second.
The last 9 digits stands for current Right Ascension, the unit current Right Ascension is 0.01 second.

:MS#
Response: “1” if command accepted, “0” The desired object is below 0 degrees altitude.
Slew to the most recently defined Right Ascension and Declination coordinates or most recently
defined Altitude and Azimuth coordinates (only works with Alt-Azi Mount). If the object is below
the horizon, this will be stated, and no slewing will occur.

:Q# 1 stop slewing

:MnXXXXX# :MsXXXXX# :MeXXXXX# :MwXXXXX#
Command motion for XXXXX milliseconds in the direction specified at the currently selected guide
rate. XXXXX is in the range of 0 to 32767.

:ST0# 1 stop tracking
:ST1# 1 start tracking

:RT0# :RT1# :RT2# :RT3# :RT4# 1
This command selects the tracking rate. It selects sidereal (:RT0#), lunar (:RT1#), solar (:RT2#),
King (:RT3#), or custom (“:RT4#”). The sidereal rate is assumed as a default by the next power up.
This command has no effect on the use of the N-S-E-W buttons.

:MP1# 1
Park to the most recently defined Right Ascension and Declination coordinates or most recently
defined Altitude and Azimuth coordinates (only works with Alt-Azi Mount). If the target is below
the horizon, this command will have no effect. In parked mode, the mount cannot slew, track, guide
or perform any movement unless a un-park command is issued. If you parked the mount and
powered it off, at the beginning of the next power up, the mount will un-park automatically.

:MP0# 1
This command un-parks the mount. If the mount is already un-parked, the command will have no
effect.

:MH# 1
This command will slew to the zero position (home position) immediately.

:AH#
“0” The mount is not at zero position (home position),
“1” The mount is at zero position (home position).

:SRn# 1
Sets the moving rate used for the N-S-E-W buttons. For n, specify an integer from 1 to 9. 1 stands
for 1x sidereal tracking rate, 2 stands for 2x, 3 stands for 8x, 4 stands for 16x, 5 stands for 64x, 6
stands for 128x, 7 stands for 256x, 8 stands for 512x, 9 stands for maximum speed. Currently, the
maximum speed of CEM60 (-EC) is 900x, the maximum speed of iEQ45 Pro (/AA) is 1400x. 64x
is assumed as a default by the next power up.

:mn# :me# :ms# :mw#
These commands have identical function as arrow key pressed. They will move mounts to N-E-S-
W direction at specified speed (may change by “:SRn#”). The mount will keep moving until a
“:qR#”, “:qD#”, and/or “:q#” sent.

:q#
This command will stop moving by arrow keys or “:mn#”, “:me#”, “:ms#”, “:mw#” command.
Slewing and tracking will not be affected.

:qR#
This command will stop moving by left and right arrow keys or “:me#”, “:mw#” command.
Slewing and tracking will not be affected.

:qD#
This commands will stop moving by up and down arrow keys or “:mn#”, “:ms#” command.
Slewing and tracking will not be affected.

:CM# 1
Calibrate mount (Sync). In equatorial mounts, the most recently defined Right Ascension and
Declination become the commanded Right Ascension and Declination respectively. In Alt-Azi
mounts, the most recently defined Altitude and Azimuth become the commanded Altitude and
Azimuth. This command assumes that the mount has been manually positioned on the proper pier
side for the calibration object. This command is ignored if slewing is in progress. This command
should be used for initial calibration. It should not be used after the mount has been tracking unless
it is known that it has not tracked across the meridian.

:Sr HH:MM:SS# 1
Defines the commanded Right Ascension, RA. Move, calibrate and park commands operate on the
most recently defined RA.

:Sd sDD*MM:SS# 1
Defines the commanded Declination, DEC. Move, calibrate and park commands operate on the most
recently defined DEC.

:FW1# YYMMDDYYMMDD# hand controller's firmware date.

:FW2# YYMMDDYYMMDD# ra dec motor board's firmware date.

:V# V1.00# version number

:MountInfo# 0011 SmartEQ Pro+

**/

#include <sstream>

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>

#include "ioptron.h"

namespace {
class ArcSeconds {
public:
    ArcSeconds() noexcept = default;
    ~ArcSeconds() noexcept = default;

    void fromString(
        std::string &s
    ) noexcept {
        int angle = std::stoi(s);
        angle_ = double(angle) / 3600.0;
        angle = std::abs(angle);
        secs_ = angle % 60;
        angle /= 60;
        mins_ = angle % 60;
        hrs_ = angle / 60;
        if (angle_ < 0.0) {
            hrs_ = - hrs_;
        }
    }

    void setEastWest() noexcept {
        if (hrs_ >= 0) {
            east_west_ = 'E';
        } else {
            hrs_ = - hrs_;
            east_west_ = 'W';
        }
    }

    std::string toString() noexcept {
        std::stringstream ss;
        ss<<angle_<<" "<<hrs_<<" "<<mins_<<"' "<<secs_<<"\"";
        if (east_west_) {
            ss<<" "<<east_west_;
        }
        return ss.str();
    }

    double angle_ = 0.0;
    int hrs_ = 0;
    int mins_ = 0;
    int secs_ = 0;
    char east_west_ = 0;
};

class IoptronImpl : public Ioptron {
public:
    IoptronImpl() noexcept = default;
    virtual ~IoptronImpl() noexcept = default;

    SerialConnection port_;
    bool is_connected_ = false;

    void connect() noexcept {
        /** open the serial port. **/
        is_connected_ = port_.open();
        if (is_connected_ == false) {
            LOG("Serial cable is not connected.");
            return;
        }

        /** required initialize. **/
        port_.write(":MountInfo#");
        auto response = port_.read(-1);
        if (response.empty()) {
            response = "MOUNT NOT CONNECTED";
            is_connected_ = false;
        } else if (response == "0011") {
            response = "IOptron SmartEQ Pro+";
        }
        LOG("IOptron Mount type [:MountInfo#]: "<<response);

        /** bail if connection failed. **/
        if (is_connected_ == false) {
            LOG("IOptron mount is not powered or the cable is not connected to the handset.");
            return;
        }

        port_.write(":Q#");
        response = port_.read(1);
        LOG("IOptron Slewing stopped: "<<response);

        port_.write(":q#");
        response = port_.read(1);
        LOG("IOptron Stopped moving by arrow keys: "<<response);

        port_.write(":RT0#");
        response = port_.read(1);
        LOG("IOptron Set sidereal tracking rate [:RT0#]: "<<response);

        port_.write(":GLS#");
        response = port_.read(0);
        //LOG("IOptron Get longitude latitude status [:GLS#]: "<<response);
        showStatus(response);

        port_.write(":GLT#");
        response = port_.read(0);
        //LOG("IOptron Get time [:GLT#]: "<<response);
        showTime(response);

        port_.write(":GEC#");
        response = port_.read(0);
        LOG("IOptron Get right ascension and declination [:GEC#]: "<<response);

        port_.write(":GAC#");
        response = port_.read(0);
        LOG("IOptron Get altitude and azimuth [:GAC#]: "<<response);

        port_.write(":GAL#");
        response = port_.read(0);
        LOG("IOptron Get altitude limit [:GAL#]: "<<response);

    #if 0
        port_.write(":ST1#");
        response = port_.read(1);
        LOG("IOptron Started tracking [:ST1#]: "<<response);

        agm::sleep::seconds(2);

        port_.write(":mn#");
        LOG("IOptron Moving north...");
        port_.write(":me#");
        LOG("IOptron Moving east...");

        agm::sleep::seconds(2);
        port_.write(":q#");
        response = port_.read(1);
        LOG("IOptron Stopped: "<<response);

        agm::sleep::seconds(2);
        port_.write(":ST0#");
        response = port_.read(1);
        LOG("IOptron Stop tracking [:ST0#]: "<<response);

        agm::sleep::seconds(2);
        port_.write(":MH#");
        response = port_.read(1);
        LOG("IOptron Slew to home position [:MH#]: "<<response);
    #endif
    }

    void showStatus(
        std::string &response
    ) noexcept {
        ArcSeconds lat;
        ArcSeconds lng;

        auto s = response.substr(7, 6);
        lat.fromString(s);
        lat.angle_ -= 90.0;
        lat.hrs_ -= 90;
        s = lat.toString();
        LOG("IOptron Status Latitude: "<<s);

        s = response.substr(0, 7);
        lng.fromString(s);
        lng.setEastWest();
        s = lng.toString();
        LOG("IOptron Status Longitude: "<<s);

        switch (response[13]) {
        case '0':
            LOG("IOptron Status GPS: none");
            break;
        case '1':
            LOG("IOptron Status GPS: no data");
            break;
        case '2':
            LOG("IOptron Status GPS: yes");
            break;
        default:
            LOG("IOptron Status GPS: unknown '"<<response[13]<<"'");
            break;
        }

        switch (response[14]) {
        case '0':
            LOG("IOptron Status System: stopped at non-zero position.");
            break;
        case '1':
            LOG("IOptron Status System: tracking with PEC disabled.");
            break;
        case '2':
            LOG("IOptron Status System: slewing.");
            break;
        case '3':
            LOG("IOptron Status System: auto-guiding.");
            break;
        case '4':
            LOG("IOptron Status System: meridian flipping.");
            break;
        case '5':
            LOG("IOptron Status System: tracking with PEC enabled.");
            break;
        case '6':
            LOG("IOptron Status System: parked.");
            break;
        case '7':
            LOG("IOptron Status System: stopped at zero position.");
            break;
        default:
            LOG("IOptron Status System: unknown '"<<response[14]<<"'");
            break;
        }

        switch (response[15]) {
        case '0':
            LOG("IOptron Status Tracking rate: sidereal.");
            break;
        case '1':
            LOG("IOptron Status Tracking rate: lunar.");
            break;
        case '2':
            LOG("IOptron Status Tracking rate: solar.");
            break;
        case '3':
            LOG("IOptron Status Tracking rate: king.");
            break;
        case '4':
            LOG("IOptron Status Tracking rate: custom.");
            break;
        default:
            LOG("IOptron Status Tracking rate: unknown '"<<response[15]<<"'");
            break;
        }

        LOG("IOptron Status Arrow key slewing rate: "<<response[16]);

        switch (response[17]) {
        case '1':
            LOG("IOptron Status Time source: RS-232 or Ethernet port.");
            break;
        case '2':
            LOG("IOptron Status Time source: Hand controller.");
            break;
        case '3':
            LOG("IOptron Status Time source: GPS.");
            break;
        default:
            LOG("IOptron Status Time source: unknown '"<<response[17]<<"'");
            break;
        }

        switch (response[18]) {
        case '0':
            LOG("IOptron Status Hemisphere: southern");
            break;
        case '1':
            LOG("IOptron Status Hemisphere: northern");
            break;
        default:
            LOG("IOptron Status Time source: unknown '"<<response[18]<<"'");
            break;
        }
    }

    void showTime(
        std::string &response
    ) noexcept {
        auto s = response.substr(0, 4);
        auto utc = std::stof(s) / 60.0;
        std::stringstream ss;
        /** YYYY/MM/DD **/
        ss<<"20"<<response[5]<<response[6];
        ss<<"/"<<response[7]<<response[8];
        ss<<"/"<<response[9]<<response[10];
        /** HH:MM::SS **/
        ss<<" "<<response[11]<<response[12];
        ss<<":"<<response[13]<<response[14];
        ss<<":"<<response[15]<<response[16];
        /** utc offset. **/
        ss<<" UTC";
        if (utc >= 0.0) {
            ss<<"+";
        }
        ss<<utc;

        LOG("IOptron Time: "<<ss.str());
    }

    void disconnect() noexcept {
        port_.close();
    }
};
}

Ioptron::Ioptron() noexcept {
}

Ioptron::~Ioptron() noexcept {
}

Ioptron *Ioptron::create() noexcept {
    auto impl = new(std::nothrow) IoptronImpl;
    return impl;
}

void Ioptron::connect() noexcept {
    auto impl = (IoptronImpl *) this;
    impl->connect();
}

void Ioptron::disconnect() noexcept {
    auto impl = (IoptronImpl *) this;
    impl->disconnect();
}
