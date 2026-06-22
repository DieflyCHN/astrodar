#pragma once
struct MoonPosition { double eclipticLongitude, eclipticLatitude, distanceKm, declination, signedElongation, elongation, illuminatedFraction; };
MoonPosition calculateMoonPosition(double jdTT, double sunEclipticLongitude);
