#include "location_config.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>

namespace {

std::string panelId(const LocationPanelKind kind, const std::string& timeZone, const double latitude, const double longitude) {
    if (kind == LocationPanelKind::TimeZone) return timeZone;
    std::ostringstream out;
    out << "坐标 " << std::fixed << std::setprecision(2) << latitude << ", " << longitude;
    return out.str();
}

std::optional<std::string> stringField(const std::string& object, const std::string& name) {
    const std::regex pattern("\\\"" + name + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) return std::nullopt;
    return match[1].str();
}

std::optional<double> numberField(const std::string& object, const std::string& name) {
    const std::regex pattern("\\\"" + name + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) return std::nullopt;
    try { return std::stod(match[1].str()); }
    catch (...) { return std::nullopt; }
}

}  // namespace

std::filesystem::path locationConfigurationPath() {
    const char* home = std::getenv("HOME");
    const std::filesystem::path homePath = home ? home : ".";
#ifdef __APPLE__
    return homePath / "Library" / "Application Support" / "astrodar" / "locations.json";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const std::filesystem::path configHome = xdg && *xdg ? xdg : homePath / ".config";
    return configHome / "astrodar" / "locations.json";
#endif
}

std::vector<LocationPanel> loadLocationPanels() {
    std::ifstream input(locationConfigurationPath());
    if (!input) return {};
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::regex objectPattern(R"(\{([^{}]*)\})");
    std::vector<LocationPanel> panels;
    for (std::sregex_iterator it(content.begin(), content.end(), objectPattern), end; it != end; ++it) {
        const std::string object = (*it)[1].str();
        const auto type = stringField(object, "type");
        if (!type) continue;
        if (*type == "timezone") {
            const auto timeZone = stringField(object, "time_zone");
            if (timeZone && !timeZone->empty()) panels.push_back({LocationPanelKind::TimeZone, panelId(LocationPanelKind::TimeZone, *timeZone, 0.0, 0.0), *timeZone});
        } else if (*type == "coordinates") {
            const auto name = stringField(object, "name");
            const auto latitude = numberField(object, "latitude");
            const auto longitude = numberField(object, "longitude");
            if (latitude && longitude && *latitude >= -90.0 && *latitude <= 90.0 && *longitude >= -180.0 && *longitude <= 180.0) {
                const std::string id = name && !name->empty()
                    ? *name : panelId(LocationPanelKind::Coordinates, "", *latitude, *longitude);
                panels.push_back({LocationPanelKind::Coordinates, id, "", *latitude, *longitude});
            }
        }
    }
    return panels;
}

bool saveLocationPanels(const std::vector<LocationPanel>& panels) {
    const std::filesystem::path path = locationConfigurationPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;

    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "{\n  \"version\": 1,\n  \"panels\": [\n";
    bool first = true;
    for (const LocationPanel& panel : panels) {
        if (!first) output << ",\n";
        first = false;
        if (panel.kind == LocationPanelKind::TimeZone) {
            output << "    {\"type\": \"timezone\", \"time_zone\": \"" << panel.timeZone << "\"}";
        } else {
            output << "    {\"type\": \"coordinates\", \"name\": \"" << panel.id << "\", \"latitude\": " << std::setprecision(12) << panel.latitudeDegrees
                   << ", \"longitude\": " << std::setprecision(12) << panel.longitudeDegrees << "}";
        }
    }
    output << "\n  ]\n}\n";
    output.close();
    if (!output) return false;
    std::filesystem::rename(temporary, path, error);
    if (!error) return true;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    return !error;
}
