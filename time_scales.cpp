#include "time_scales.hpp"

#include <array>

namespace {

struct LeapSecond {
    double effectiveJDUTC;
    int taiMinusUtc;
};

constexpr double gregorianMidnightJD(int year, int month, int day) {
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    const int jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    return static_cast<double>(jdn) - 0.5;
}

constexpr std::array<LeapSecond, 28> LEAP_SECONDS = {{
    {gregorianMidnightJD(1972, 1, 1), 10}, {gregorianMidnightJD(1972, 7, 1), 11},
    {gregorianMidnightJD(1973, 1, 1), 12}, {gregorianMidnightJD(1974, 1, 1), 13},
    {gregorianMidnightJD(1975, 1, 1), 14}, {gregorianMidnightJD(1976, 1, 1), 15},
    {gregorianMidnightJD(1977, 1, 1), 16}, {gregorianMidnightJD(1978, 1, 1), 17},
    {gregorianMidnightJD(1979, 1, 1), 18}, {gregorianMidnightJD(1980, 1, 1), 19},
    {gregorianMidnightJD(1981, 7, 1), 20}, {gregorianMidnightJD(1982, 7, 1), 21},
    {gregorianMidnightJD(1983, 7, 1), 22}, {gregorianMidnightJD(1985, 7, 1), 23},
    {gregorianMidnightJD(1988, 1, 1), 24}, {gregorianMidnightJD(1990, 1, 1), 25},
    {gregorianMidnightJD(1991, 1, 1), 26}, {gregorianMidnightJD(1992, 7, 1), 27},
    {gregorianMidnightJD(1993, 7, 1), 28}, {gregorianMidnightJD(1994, 7, 1), 29},
    {gregorianMidnightJD(1996, 1, 1), 30}, {gregorianMidnightJD(1997, 7, 1), 31},
    {gregorianMidnightJD(1999, 1, 1), 32}, {gregorianMidnightJD(2006, 1, 1), 33},
    {gregorianMidnightJD(2009, 1, 1), 34}, {gregorianMidnightJD(2012, 7, 1), 35},
    {gregorianMidnightJD(2015, 7, 1), 36}, {gregorianMidnightJD(2017, 1, 1), 37},
}};

}  // namespace

std::optional<double> utcJulianDateToTT(double jdUTC) {
    if (jdUTC < LEAP_SECONDS.front().effectiveJDUTC) {
        return std::nullopt;
    }

    int taiMinusUtc = LEAP_SECONDS.front().taiMinusUtc;
    for (const LeapSecond& entry : LEAP_SECONDS) {
        if (jdUTC < entry.effectiveJDUTC) {
            break;
        }
        taiMinusUtc = entry.taiMinusUtc;
    }

    return jdUTC + (taiMinusUtc + 32.184) / 86400.0;
}

std::optional<double> ttJulianDateToUTC(double jdTT) {
    int taiMinusUtc = LEAP_SECONDS.back().taiMinusUtc;
    for (int iteration = 0; iteration < 3; ++iteration) {
        const double jdUTC = jdTT - (taiMinusUtc + 32.184) / 86400.0;
        if (jdUTC < LEAP_SECONDS.front().effectiveJDUTC) {
            return std::nullopt;
        }

        int updatedTaiMinusUtc = LEAP_SECONDS.front().taiMinusUtc;
        for (const LeapSecond& entry : LEAP_SECONDS) {
            if (jdUTC < entry.effectiveJDUTC) {
                break;
            }
            updatedTaiMinusUtc = entry.taiMinusUtc;
        }
        if (updatedTaiMinusUtc == taiMinusUtc) {
            return jdUTC;
        }
        taiMinusUtc = updatedTaiMinusUtc;
    }

    return jdTT - (taiMinusUtc + 32.184) / 86400.0;
}
