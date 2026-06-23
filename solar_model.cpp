#include "solar_model.hpp"
#include "earth_orientation.hpp"

#include <cmath>

namespace {

constexpr double J2000 = 2451545.0;
constexpr double JULIAN_CENTURY_DAYS = 36525.0;
constexpr double JULIAN_MILLENNIUM_DAYS = 365250.0;
constexpr double PI = 3.14159265358979323846;

struct VSOPTerm {
    int coordinate;
    int order;
    double amplitude;
    double phase;
    double frequency;
};

// VSOP87D Earth coefficients, published by IMCCE. This generated local table
// contains 2,425 terms and is evaluated entirely in-process.
constexpr VSOPTerm VSOP87D_EARTH_TERMS[] = {
#include "vsop87d_earth.inc"
};

double degreesToRadians(double degrees) {
    return degrees * PI / 180.0;
}

double radiansToDegrees(double radians) {
    return radians * 180.0 / PI;
}

double evaluateVSOP87D(int coordinate, double tau) {
    double coordinateValue = 0.0;
    for (const VSOPTerm& term : VSOP87D_EARTH_TERMS) {
        if (term.coordinate == coordinate) {
            coordinateValue += term.amplitude * std::cos(term.phase + term.frequency * tau) *
                               std::pow(tau, term.order);
        }
    }
    return coordinateValue;
}

}  // namespace

SolarPosition calculateSolarPosition(double jdTT) {
    const double T = (jdTT - J2000) / JULIAN_CENTURY_DAYS;
    const double tau = (jdTT - J2000) / JULIAN_MILLENNIUM_DAYS;

    // VSOP87D returns Earth heliocentric L, B and R in the dynamical ecliptic
    // and equinox of date. Reversing that vector gives the geometric Sun.
    const double earthLongitude = evaluateVSOP87D(1, tau);
    const double earthLatitude = evaluateVSOP87D(2, tau);
    const double distanceAU = evaluateVSOP87D(3, tau);
    const double geometricLongitude = normalizeDegrees(radiansToDegrees(earthLongitude) + 180.0);
    const double geometricLatitude = -radiansToDegrees(earthLatitude);

    // VSOP87D is already referred to the dynamical ecliptic and equinox of
    // date. Therefore no J2000-to-date longitude rotation is applied here.
    // Apply nutation and annual aberration to obtain apparent coordinates.
    const double fk5Longitude = geometricLongitude;
    const double fk5Latitude = geometricLatitude;
    const double apparentLongitude = normalizeDegrees(
        fk5Longitude + nutationInLongitudeDegrees(T) - (20.4898 / 3600.0) / distanceAU);

    const double meanObliquity = meanObliquityDegrees(T);
    const double obliquity = meanObliquity + nutationInObliquityDegrees(T);
    const double apparentLongitudeRadians = degreesToRadians(apparentLongitude);
    const double latitudeRadians = degreesToRadians(fk5Latitude);
    const double obliquityRadians = degreesToRadians(obliquity);
    const double rightAscension = normalizeDegrees(radiansToDegrees(std::atan2(
        std::sin(apparentLongitudeRadians) * std::cos(obliquityRadians) -
            std::tan(latitudeRadians) * std::sin(obliquityRadians),
        std::cos(apparentLongitudeRadians))));
    const double declination = radiansToDegrees(std::asin(
        std::sin(latitudeRadians) * std::cos(obliquityRadians) +
        std::cos(latitudeRadians) * std::sin(obliquityRadians) * std::sin(apparentLongitudeRadians)));

    // The equation of time retains the standard low-order expression for now;
    // it is independent of the VSOP87 longitude and does not drive the terms.
    const double meanLongitude = normalizeDegrees(
        280.46646 + T * (36000.76983 + 0.0003032 * T));
    const double meanAnomaly = normalizeDegrees(
        357.52911 + T * (35999.05029 - 0.0001537 * T));
    const double eccentricity =
        0.016708634 - T * (0.000042037 + 0.0000001267 * T);
    const double anomalyRadians = degreesToRadians(meanAnomaly);
    const double y = std::pow(std::tan(obliquityRadians / 2.0), 2.0);
    const double meanLongitudeRadians = degreesToRadians(meanLongitude);
    const double equationOfTimeDegrees =
        y * std::sin(2.0 * meanLongitudeRadians) -
        2.0 * eccentricity * std::sin(anomalyRadians) +
        4.0 * eccentricity * y * std::sin(anomalyRadians) * std::cos(2.0 * meanLongitudeRadians) -
        0.5 * y * y * std::sin(4.0 * meanLongitudeRadians) -
        1.25 * eccentricity * eccentricity * std::sin(2.0 * anomalyRadians);

    return {
        apparentLongitude,
        fk5Longitude,
        fk5Latitude,
        distanceAU,
        rightAscension,
        declination,
        4.0 * radiansToDegrees(equationOfTimeDegrees),
    };
}
