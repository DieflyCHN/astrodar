#pragma once

// Angular fields are in degrees. jdTT must be a Julian Date in Terrestrial
// Time (TT), not UTC; equationOfTimeMinutes is in minutes.
struct SolarPosition {
    double apparentEclipticLongitude;
    double trueEclipticLongitude;
    double eclipticLatitude;
    double distanceAU;
    double rightAscension;
    double declination;
    double equationOfTimeMinutes;
};

SolarPosition calculateSolarPosition(double jdTT);
