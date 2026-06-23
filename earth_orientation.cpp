#include "earth_orientation.hpp"

#include <cmath>

namespace {
constexpr double pi = 3.14159265358979323846;
double radians(double degrees) { return degrees * pi / 180.0; }
}

double normalizeDegrees(double degrees) {
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

double meanObliquityDegrees(double t) {
    // IAU 2006 polynomial, arcseconds converted to degrees.
    const double arcseconds = 84381.406 - 46.836769 * t - 0.0001831 * t * t +
        0.00200340 * t * t * t - 0.000000576 * t * t * t * t - 0.0000000434 * t * t * t * t * t;
    return arcseconds / 3600.0;
}

double precessionInLongitudeDegrees(double t) {
    return (5029.0966 * t + 1.11113 * t * t - 0.000006 * t * t * t) / 3600.0;
}

double nutationInLongitudeDegrees(double t) {
    const double omega = 125.04452 - t * (1934.136261 - t * (0.0020708 + t / 450000.0));
    const double sunMeanLongitude = normalizeDegrees(280.4665 + 36000.7698 * t);
    const double moonMeanLongitude = normalizeDegrees(218.3165 + 481267.8813 * t);
    const double arcseconds = -17.20 * std::sin(radians(omega)) -
        1.32 * std::sin(radians(2.0 * sunMeanLongitude)) -
        0.23 * std::sin(radians(2.0 * moonMeanLongitude)) +
        0.21 * std::sin(radians(2.0 * omega));
    return arcseconds / 3600.0;
}

double nutationInObliquityDegrees(double t) {
    const double omega = 125.04452 - t * (1934.136261 - t * (0.0020708 + t / 450000.0));
    const double sunMeanLongitude = normalizeDegrees(280.4665 + 36000.7698 * t);
    const double moonMeanLongitude = normalizeDegrees(218.3165 + 481267.8813 * t);
    const double arcseconds = 9.20 * std::cos(radians(omega)) +
        0.57 * std::cos(radians(2.0 * sunMeanLongitude)) +
        0.10 * std::cos(radians(2.0 * moonMeanLongitude)) -
        0.09 * std::cos(radians(2.0 * omega));
    return arcseconds / 3600.0;
}
