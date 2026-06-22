#pragma once

#include <optional>

// Converts a UTC Julian Date to TT. UTC prior to 1972-01-01 is intentionally
// rejected because its historical TAI-UTC relation is not a whole-second leap
// second table.
std::optional<double> utcJulianDateToTT(double jdUTC);

// Converts TT back to UTC using the same leap-second table. UTC before 1972
// is unsupported for the same reason as utcJulianDateToTT.
std::optional<double> ttJulianDateToUTC(double jdTT);
