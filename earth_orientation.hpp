#pragma once

// Shared Earth-orientation quantities. Inputs are Julian centuries TT from J2000.
double normalizeDegrees(double degrees);
double meanObliquityDegrees(double centuriesTT);
double precessionInLongitudeDegrees(double centuriesTT);
double nutationInLongitudeDegrees(double centuriesTT);
double nutationInObliquityDegrees(double centuriesTT);
