#include "solar_events.hpp"

#include "solar_model.hpp"
#include "time_scales.hpp"

#include <cmath>
#include <cstdlib>
#include <mutex>

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double SUNRISE_ZENITH_DEGREES = 90.833;

double rad(double degrees) { return degrees * PI / 180.0; }

double unixToJD(std::time_t value) {
    return static_cast<double>(value) / 86400.0 + 2440587.5;
}

std::int64_t gregorianToJDN(int year, int month, int day) {
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365LL * y + y / 4 - y / 100 + y / 400 - 32045;
}

std::time_t localCivilToUnix(const ObserverLocation& location, int year, int month, int day, int hour) {
    static std::mutex timezoneMutex;
    std::lock_guard<std::mutex> lock(timezoneMutex);
    const char* oldTZ = std::getenv("TZ");
    const std::string savedTZ = oldTZ ? oldTZ : "";
    const bool hadTZ = oldTZ != nullptr;
    setenv("TZ", location.timeZone.c_str(), 1);
    tzset();
    tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_isdst = -1;
    const std::time_t result = std::mktime(&value);
    if (hadTZ) setenv("TZ", savedTZ.c_str(), 1); else unsetenv("TZ");
    tzset();
    return result;
}
}

SolarEvents calculateSolarEvents(const ObserverLocation& location, int year, int month, int day) {
    const std::time_t localMidnight = localCivilToUnix(location, year, month, day, 0);
    const std::time_t localNoon = localCivilToUnix(location, year, month, day, 12);
    const double localNoonJD = static_cast<double>(gregorianToJDN(year, month, day)) + 0.0;
    const int timezoneMinutes = static_cast<int>(std::llround((localNoonJD - unixToJD(localNoon)) * 1440.0));
    const auto jdTT = utcJulianDateToTT(unixToJD(localNoon));
    if (!jdTT) return {};
    const SolarPosition sun = calculateSolarPosition(*jdTT);
    const double latitude = rad(location.latitudeDegrees);
    const double declination = rad(sun.declination);
    const double noonMinutes = 720.0 - 4.0 * location.longitudeDegrees - sun.equationOfTimeMinutes + timezoneMinutes;
    const std::time_t noon = localMidnight + static_cast<std::time_t>(std::llround(noonMinutes * 60.0));
    const auto crossingsAt = [&](double altitudeDegrees) {
        const double cosHourAngle = (std::sin(rad(altitudeDegrees)) - std::sin(latitude) * std::sin(declination)) /
            (std::cos(latitude) * std::cos(declination));
        if (cosHourAngle < -1.0 || cosHourAngle > 1.0) {
            return std::pair<std::optional<std::time_t>, std::optional<std::time_t>>{};
        }
        const double hourAngleMinutes = 4.0 * std::acos(cosHourAngle) * 180.0 / PI;
        return std::pair{
            std::optional<std::time_t>{localMidnight + static_cast<std::time_t>(std::llround((noonMinutes - hourAngleMinutes) * 60.0))},
            std::optional<std::time_t>{localMidnight + static_cast<std::time_t>(std::llround((noonMinutes + hourAngleMinutes) * 60.0))},
        };
    };
    const auto standard = crossingsAt(-SUNRISE_ZENITH_DEGREES + 90.0);
    const auto civil = crossingsAt(-6.0);
    const auto nautical = crossingsAt(-12.0);
    const auto astronomical = crossingsAt(-18.0);
    return {standard.first, noon, standard.second, civil.first, civil.second,
            nautical.first, nautical.second, astronomical.first, astronomical.second};
}
