#include "solar_time.hpp"

#include <cmath>

LocalSolarTime calculateLocalSolarTime(std::time_t utc, double longitudeDegrees,
                                       double equationOfTimeMinutes) {
    const double correctionSeconds = (longitudeDegrees * 4.0 + equationOfTimeMinutes) * 60.0;
    const std::time_t apparentLocalTime = utc + static_cast<std::time_t>(std::llround(correctionSeconds));
    tm value{};
    gmtime_r(&apparentLocalTime, &value);
    return {value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
            value.tm_hour, value.tm_min, value.tm_sec, value.tm_wday};
}
