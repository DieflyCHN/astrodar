#pragma once
struct MoonPosition {
    double eclipticLongitude;
    double eclipticLatitude;
    double distanceKm;
    double rightAscension;
    double declination;
    double horizontalParallax;
    double angularRadius;
    double signedElongation;
    double elongation;
    double illuminatedFraction;
};
struct TopocentricMoonPosition {
    double rightAscension;
    double declination;
    double azimuth;
    double trueAltitude;
};
MoonPosition calculateMoonPosition(double jdTT, double sunEclipticLongitude);
TopocentricMoonPosition calculateTopocentricMoonPosition(const MoonPosition& geocentric, double jdUTC,
                                                         double latitudeDegrees, double longitudeDegrees,
                                                         double elevationMeters = 0.0);
