#include "time_zone_reference.hpp"

#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::optional<double> parseIanaCoordinatePart(const std::string& part, const int degreeDigits) {
    if (part.size() != static_cast<std::size_t>(1 + degreeDigits + 2) &&
        part.size() != static_cast<std::size_t>(1 + degreeDigits + 4)) return std::nullopt;
    if (part[0] != '+' && part[0] != '-') return std::nullopt;
    try {
        const double degrees = std::stod(part.substr(1, degreeDigits));
        const double minutes = std::stod(part.substr(1 + degreeDigits, 2));
        const double seconds = part.size() == static_cast<std::size_t>(1 + degreeDigits + 4)
            ? std::stod(part.substr(1 + degreeDigits + 2, 2)) : 0.0;
        if (minutes >= 60.0 || seconds >= 60.0) return std::nullopt;
        const double result = degrees + minutes / 60.0 + seconds / 3600.0;
        return part[0] == '-' ? -result : result;
    } catch (...) { return std::nullopt; }
}

std::optional<TimeZoneReference> parseIanaCoordinates(const std::string& compact) {
    const std::size_t longitudeStart = compact.find_first_of("+-", 1);
    if (longitudeStart == std::string::npos) return std::nullopt;
    const auto latitude = parseIanaCoordinatePart(compact.substr(0, longitudeStart), 2);
    const auto longitude = parseIanaCoordinatePart(compact.substr(longitudeStart), 3);
    if (!latitude || !longitude || std::abs(*latitude) > 90.0 || std::abs(*longitude) > 180.0) return std::nullopt;
    return TimeZoneReference{*latitude, *longitude};
}

}  // namespace

std::optional<TimeZoneReference> findTimeZoneReference(const std::string& timeZone) {
    const std::vector<std::string> paths = {
        "/usr/share/zoneinfo/zone1970.tab",
        "/usr/share/zoneinfo/zone.tab",
        "/var/db/timezone/zoneinfo/zone1970.tab",
    };
    for (const std::string& path : paths) {
        std::ifstream input(path);
        if (!input) continue;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line);
            std::string countries;
            std::string coordinates;
            std::string zone;
            if (!(row >> countries >> coordinates >> zone) || zone != timeZone) continue;
            if (const auto reference = parseIanaCoordinates(coordinates)) return reference;
        }
    }
    return std::nullopt;
}

std::vector<std::string> listIanaTimeZones() {
    // zone.tab 较完整；缺失时退回 zone1970.tab。
    const std::vector<std::string> paths = {
        "/usr/share/zoneinfo/zone.tab",
        "/usr/share/zoneinfo/zone1970.tab",
        "/var/db/timezone/zoneinfo/zone.tab",
    };
    for (const std::string& path : paths) {
        std::ifstream input(path);
        if (!input) continue;
        std::vector<std::string> zones;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line);
            std::string countries;
            std::string coordinates;
            std::string zone;
            if (row >> countries >> coordinates >> zone) zones.push_back(zone);
        }
        std::sort(zones.begin(), zones.end());
        zones.erase(std::unique(zones.begin(), zones.end()), zones.end());
        if (!zones.empty()) return zones;
    }
    return {};
}
