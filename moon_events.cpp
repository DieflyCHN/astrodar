#include "moon_events.hpp"

#include "moon_model.hpp"
#include "solar_model.hpp"
#include "solar_time.hpp"
#include "time_scales.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace {
double unixToJD(std::time_t instant) { return static_cast<double>(instant) / 86400.0 + 2440587.5; }
std::time_t utcMidnight(int year, int month, int day) {
    const int a = (14 - month) / 12, y = year + 4800 - a, m = month + 12 * a - 3;
    const long long jdn = day + (153 * m + 2) / 5 + 365LL * y + y / 4 - y / 100 + y / 400 - 32045;
    return static_cast<std::time_t>(std::llround((static_cast<double>(jdn) - 0.5 - 2440587.5) * 86400.0));
}
bool sameSolarDate(const LocalSolarTime& value, int year, int month, int day) {
    return value.year == year && value.month == month && value.day == day;
}
bool isRequestedSolarDate(const ObserverLocation& location, std::time_t instant, int year, int month, int day) {
    const auto jdTT = utcJulianDateToTT(unixToJD(instant));
    if (!jdTT) return false;
    const double eot = calculateSolarPosition(*jdTT).equationOfTimeMinutes;
    return sameSolarDate(calculateLocalSolarTime(instant, location.longitudeDegrees, eot), year, month, day);
}
double riseSetFunctionAt(const ObserverLocation& location, std::time_t instant) {
    const auto jdTT = utcJulianDateToTT(unixToJD(instant));
    if (!jdTT) return -90.0;
    const SolarPosition sun = calculateSolarPosition(*jdTT);
    const MoonPosition moon = calculateMoonPosition(*jdTT, sun.apparentEclipticLongitude);
    const double altitude = calculateTopocentricMoonPosition(moon, unixToJD(instant), location.latitudeDegrees,
        location.longitudeDegrees, location.elevationMeters).trueAltitude;
    return altitude + 34.0 / 60.0 + moon.angularRadius;
}
double altitudeAt(const ObserverLocation& location, std::time_t instant) {
    const auto jdTT = utcJulianDateToTT(unixToJD(instant));
    if (!jdTT) return -90.0;
    const SolarPosition sun = calculateSolarPosition(*jdTT);
    const MoonPosition moon = calculateMoonPosition(*jdTT, sun.apparentEclipticLongitude);
    return calculateTopocentricMoonPosition(moon, unixToJD(instant), location.latitudeDegrees,
        location.longitudeDegrees, location.elevationMeters).trueAltitude;
}
std::string cacheKey(const ObserverLocation& location, int year, int month, int day) {
    std::ostringstream key;
    key << year << '-' << month << '-' << day << ':' << std::fixed << std::setprecision(7)
        << location.latitudeDegrees << ':' << location.longitudeDegrees << ':' << location.elevationMeters;
    return key.str();
}
}  // namespace

MoonEvents calculateMoonEvents(const ObserverLocation& location, int year, int month, int day) {
    static std::string previousKey;
    static MoonEvents previous;
    const std::string key = cacheKey(location, year, month, day);
    if (key == previousKey) return previous;

    const std::time_t nominalMidnight = utcMidnight(year, month, day);
    const std::time_t start = nominalMidnight - 13 * 3600;
    const std::time_t end = nominalMidnight + 37 * 3600;
    constexpr std::time_t step = 600;
    MoonEvents result;
    std::time_t transit = start;
    double maximumAltitude = -91.0;
    std::time_t left = start;
    double leftAltitude = riseSetFunctionAt(location, left);
    for (std::time_t right = start + step; right <= end; right += step) {
        const double rightRiseSetValue = riseSetFunctionAt(location, right);
        const double rightAltitude = altitudeAt(location, right);
        if (rightAltitude > maximumAltitude && isRequestedSolarDate(location, right, year, month, day)) {
            maximumAltitude = rightAltitude;
            transit = right;
        }
        if ((leftAltitude < 0.0 && rightRiseSetValue >= 0.0) || (leftAltitude >= 0.0 && rightRiseSetValue < 0.0)) {
            std::time_t low = left, high = right;
            const bool rising = leftAltitude < rightRiseSetValue;
            while (high - low > 1) {
                const std::time_t middle = low + (high - low) / 2;
                const double altitude = riseSetFunctionAt(location, middle);
                if ((rising && altitude >= 0.0) || (!rising && altitude < 0.0)) high = middle;
                else low = middle;
            }
            const auto jdTT = utcJulianDateToTT(unixToJD(high));
            const double eot = jdTT ? calculateSolarPosition(*jdTT).equationOfTimeMinutes : 0.0;
            if (sameSolarDate(calculateLocalSolarTime(high, location.longitudeDegrees, eot), year, month, day)) {
                if (rising) result.standardRise = high;
                else result.standardSet = high;
            }
        }
        left = right;
        leftAltitude = rightRiseSetValue;
    }
    if (maximumAltitude > -91.0) {
        std::time_t low = transit - step, high = transit + step;
        for (int iteration = 0; iteration < 24; ++iteration) {
            const std::time_t first = low + (high - low) / 3;
            const std::time_t second = high - (high - low) / 3;
            if (altitudeAt(location, first) < altitudeAt(location, second)) low = first;
            else high = second;
        }
        const std::time_t candidateTransit = (low + high) / 2;
        if (isRequestedSolarDate(location, candidateTransit, year, month, day)) result.upperTransit = candidateTransit;
    }
    previousKey = key;
    previous = result;
    return result;
}
