#pragma once

struct SolarTermEvent {
    int longitudeDegrees;
    const char* chineseName;
    double jdTT;
};

// Finds the next 15-degree solar-longitude crossing after jdTT. The result's
// accuracy is bounded by the active solar model, not by the root solver.
SolarTermEvent nextSolarTerm(double jdTT);

// Finds the 15-degree solar-longitude interval containing jdTT and returns
// the event that began that interval.
SolarTermEvent currentSolarTerm(double jdTT);

// Returns the requested solar term that occurs within the Gregorian year.
// longitudeDegrees must be one of 0, 15, ..., 345.
SolarTermEvent solarTermInGregorianYear(int gregorianYear, int longitudeDegrees);
