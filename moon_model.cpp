#include "moon_model.hpp"
#include "elp82b_model.hpp"
#include "earth_orientation.hpp"

#include <cmath>

namespace {
constexpr double pi = 3.14159265358979323846;
double radians(double value) { return value * pi / 180.0; }
double normalize(double value) { value = std::fmod(value, 360.0); return value < 0.0 ? value + 360.0 : value; }
double signedDifference(double left, double right) { return normalize(left - right + 180.0) - 180.0; }
MoonPosition finalize(double jdTT, double longitude, double latitude, double distance, double sunLongitude) {
    const double t = (jdTT - 2451545.0) / 36525.0;
    const double obliquity = radians(meanObliquityDegrees(t) + nutationInObliquityDegrees(t));
    const double lon = radians(longitude), lat = radians(latitude);
    const double ra = normalize(std::atan2(std::sin(lon) * std::cos(obliquity) - std::tan(lat) * std::sin(obliquity), std::cos(lon)) * 180.0 / pi);
    const double dec = std::asin(std::sin(lat) * std::cos(obliquity) + std::cos(lat) * std::sin(obliquity) * std::sin(lon)) * 180.0 / pi;
    const double parallax = std::asin(6378.137 / distance) * 180.0 / pi;
    const double radius = std::asin(1737.4 / distance) * 180.0 / pi;
    const double signedElongation = signedDifference(longitude, sunLongitude);
    const double elongation = std::abs(signedElongation);
    return {longitude, latitude, distance, ra, dec, parallax, radius, signedElongation, elongation,
            (1.0 - std::cos(radians(elongation))) / 2.0};
}
}  // namespace

MoonPosition calculateMoonPosition(double jdTT, double sunEclipticLongitude) {
    if (const auto elp = calculateELP82BPosition(jdTT)) {
        const double t = (jdTT - 2451545.0) / 36525.0;
        const double longitudeOfDate = normalize(elp->longitudeDegrees + precessionInLongitudeDegrees(t) + nutationInLongitudeDegrees(t));
        return finalize(jdTT, longitudeOfDate, elp->latitudeDegrees, elp->distanceKm, sunEclipticLongitude);
    }
    // 仅在 ELP 系数文件不可用时保留旧截断模型，保证开发期仍可运行。
    const double t = (jdTT - 2451545.0) / 36525.0;
    const double L = normalize(218.3164477 + 481267.88123421 * t);
    const double D = normalize(297.8501921 + 445267.1114034 * t);
    const double M = normalize(357.5291092 + 35999.0502909 * t);
    const double Q = normalize(134.9633964 + 477198.8675055 * t);
    const double F = normalize(93.272095 + 483202.0175233 * t);
    const double E = 1.0 - 0.002516 * t - 0.0000074 * t * t;
    const auto sine = [](double value) { return std::sin(radians(value)); };
    const auto cosine = [](double value) { return std::cos(radians(value)); };
    const double longitudeCorrection = (6288774 * sine(Q) + 1274027 * sine(2 * D - Q) + 658314 * sine(2 * D) + 213618 * sine(2 * Q) -
        185116 * E * sine(M) - 114332 * sine(2 * F) + 58793 * sine(2 * D - 2 * Q) + 57066 * E * sine(2 * D - M - Q) + 53322 * sine(2 * D + Q)) / 1e6;
    const double latitude = (5128122 * sine(F) + 280602 * sine(Q + F) + 277693 * sine(Q - F) + 173237 * sine(2 * D - F) +
        55413 * sine(2 * D - Q + F) + 46271 * sine(2 * D - Q - F) + 32573 * sine(2 * D + F)) / 1e6;
    const double distance = 385000.56 + (-20905355 * cosine(Q) - 3699111 * cosine(2 * D - Q) - 2955968 * cosine(2 * D) -
        569925 * cosine(2 * Q) + 48888 * E * cosine(M) - 246158 * cosine(2 * D - 2 * Q)) / 1000.0;
    return finalize(jdTT, normalize(L + longitudeCorrection), latitude, distance, sunEclipticLongitude);
}

TopocentricMoonPosition calculateTopocentricMoonPosition(const MoonPosition& geocentric, double jdUTC,
                                                         double latitudeDegrees, double longitudeDegrees,
                                                         double elevationMeters) {
    const double latitude = radians(latitudeDegrees);
    const double geocentricRa = radians(geocentric.rightAscension);
    const double geocentricDec = radians(geocentric.declination);
    const double parallax = radians(geocentric.horizontalParallax);
    const double flatteningRatio = 0.99664719;
    const double u = std::atan(flatteningRatio * std::tan(latitude));
    const double elevationRatio = elevationMeters / 6378137.0;
    const double rhoSinPhi = flatteningRatio * std::sin(u) + elevationRatio * std::sin(latitude);
    const double rhoCosPhi = std::cos(u) + elevationRatio * std::cos(latitude);
    const double centuries = (jdUTC - 2451545.0) / 36525.0;
    const double gmst = normalize(280.46061837 + 360.98564736629 * (jdUTC - 2451545.0) +
        0.000387933 * centuries * centuries - centuries * centuries * centuries / 38710000.0);
    const double localSiderealTime = radians(normalize(gmst + longitudeDegrees));
    const double hourAngle = localSiderealTime - geocentricRa;
    const double deltaRa = std::atan2(-rhoCosPhi * std::sin(parallax) * std::sin(hourAngle),
        std::cos(geocentricDec) - rhoCosPhi * std::sin(parallax) * std::cos(hourAngle));
    const double topocentricRa = geocentricRa + deltaRa;
    const double topocentricDec = std::atan2((std::sin(geocentricDec) - rhoSinPhi * std::sin(parallax)) * std::cos(deltaRa),
        std::cos(geocentricDec) - rhoCosPhi * std::sin(parallax) * std::cos(hourAngle));
    const double topocentricHourAngle = localSiderealTime - topocentricRa;
    const double altitude = std::asin(std::sin(latitude) * std::sin(topocentricDec) +
        std::cos(latitude) * std::cos(topocentricDec) * std::cos(topocentricHourAngle));
    const double azimuth = normalize(std::atan2(std::sin(topocentricHourAngle),
        std::cos(topocentricHourAngle) * std::sin(latitude) - std::tan(topocentricDec) * std::cos(latitude)) * 180.0 / pi + 180.0);
    return {normalize(topocentricRa * 180.0 / pi), topocentricDec * 180.0 / pi, azimuth, altitude * 180.0 / pi};
}
