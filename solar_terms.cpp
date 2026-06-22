#include "solar_terms.hpp"

#include "solar_model.hpp"

#include <cmath>

namespace {

constexpr double SOLAR_MOTION_DEGREES_PER_DAY = 0.98564736;
constexpr double ROOT_TOLERANCE_DEGREES = 1e-10;
constexpr int MAX_ITERATIONS = 64;

const char* SOLAR_TERM_NAMES[24] = {
    "春分", "清明", "谷雨", "立夏", "小满", "芒种", "夏至", "小暑",
    "大暑", "立秋", "处暑", "白露", "秋分", "寒露", "霜降", "立冬",
    "小雪", "大雪", "冬至", "小寒", "大寒", "立春", "雨水", "惊蛰",
};

double normalizeDegrees(double degrees) {
    double result = std::fmod(degrees, 360.0);
    return result < 0.0 ? result + 360.0 : result;
}

double signedAngleDifference(double lhs, double rhs) {
    return normalizeDegrees(lhs - rhs + 180.0) - 180.0;
}

double solveLongitude(double estimateJDTT, double targetLongitude) {
    // Solar longitude is strictly increasing over this short interval.  The
    // initial estimate is within a fraction of a day, while the bracket spans
    // ten days, so bisection is more robust than an unbounded Newton step.
    double lowerJDTT = estimateJDTT - 5.0;
    double upperJDTT = estimateJDTT + 5.0;
    double lowerError = signedAngleDifference(
        calculateSolarPosition(lowerJDTT).apparentEclipticLongitude, targetLongitude);
    double upperError = signedAngleDifference(
        calculateSolarPosition(upperJDTT).apparentEclipticLongitude, targetLongitude);

    if (lowerError > 0.0 || upperError < 0.0) {
        return estimateJDTT;
    }

    for (int iteration = 0; iteration < MAX_ITERATIONS; ++iteration) {
        const double midpointJDTT = (lowerJDTT + upperJDTT) / 2.0;
        const double error = signedAngleDifference(
            calculateSolarPosition(midpointJDTT).apparentEclipticLongitude, targetLongitude);
        if (std::abs(error) < ROOT_TOLERANCE_DEGREES) {
            return midpointJDTT;
        }

        if (error < 0.0) {
            lowerJDTT = midpointJDTT;
            lowerError = error;
        } else {
            upperJDTT = midpointJDTT;
            upperError = error;
        }
    }
    return (lowerJDTT + upperJDTT) / 2.0;
}

double gregorianMidnightJD(int year, int month, int day) {
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    const int jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    return static_cast<double>(jdn) - 0.5;
}

}  // namespace

SolarTermEvent nextSolarTerm(double jdTT) {
    const double longitude = calculateSolarPosition(jdTT).apparentEclipticLongitude;
    int index = static_cast<int>(std::floor(longitude / 15.0)) + 1;
    index %= 24;
    const double targetLongitude = index * 15.0;
    const double forwardDistance = normalizeDegrees(targetLongitude - longitude);
    const double estimateJDTT = jdTT + forwardDistance / SOLAR_MOTION_DEGREES_PER_DAY;

    return {index * 15, SOLAR_TERM_NAMES[index], solveLongitude(estimateJDTT, targetLongitude)};
}

SolarTermEvent currentSolarTerm(double jdTT) {
    const double longitude = calculateSolarPosition(jdTT).apparentEclipticLongitude;
    const int index = static_cast<int>(std::floor(longitude / 15.0));
    const double targetLongitude = index * 15.0;
    const double backwardDistance = normalizeDegrees(longitude - targetLongitude);
    const double estimateJDTT = jdTT - backwardDistance / SOLAR_MOTION_DEGREES_PER_DAY;

    return {index * 15, SOLAR_TERM_NAMES[index], solveLongitude(estimateJDTT, targetLongitude)};
}

SolarTermEvent solarTermInGregorianYear(int gregorianYear, int longitudeDegrees) {
    const int normalizedLongitude = static_cast<int>(normalizeDegrees(longitudeDegrees));
    const int index = normalizedLongitude / 15;
    if (normalizedLongitude % 15 != 0) {
        return {};
    }

    // 小寒至惊蛰 occur in January–March, so their March-equinox reference
    // date belongs to the previous Gregorian year.
    const int referenceYear = index >= 19 ? gregorianYear - 1 : gregorianYear;
    const double estimateJDTT = gregorianMidnightJD(referenceYear, 3, 20) +
        normalizedLongitude / 360.0 * 365.2422;
    return {normalizedLongitude, SOLAR_TERM_NAMES[index],
            solveLongitude(estimateJDTT, normalizedLongitude)};
}
