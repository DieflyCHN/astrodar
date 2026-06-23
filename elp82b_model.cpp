#include "elp82b_model.hpp"
#include "elp82b_embedded.hpp"

#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double radiansPerDegree = pi / 180.0;
constexpr double arcsecondsPerRadian = 648000.0 / pi;
constexpr double kilometersScale = 384747.9806448954 / 384747.9806743165;

struct MainTerm { std::array<int, 4> multipliers; std::array<double, 7> coefficients; };
struct PerturbationTerm { int zeta; std::array<int, 4> lunar; double phase; double amplitude; };
struct PlanetaryTerm { std::array<int, 11> multipliers; double phase; double amplitude; };
struct Data { std::array<std::vector<MainTerm>, 3> main; std::array<std::vector<PerturbationTerm>, 36> perturbations; std::array<std::vector<PlanetaryTerm>, 36> planetary; bool loaded = false; };

bool load(Data& data) {
    for (int fileNumber = 1; fileNumber <= 36; ++fileNumber) {
        const std::string_view text = embeddedELP82BFile(fileNumber);
        if (text.empty()) return false;
        std::istringstream input{std::string(text)};
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream row(line);
            if (fileNumber <= 3) {
                MainTerm term;
                if (!(row >> term.multipliers[0] >> term.multipliers[1] >> term.multipliers[2] >> term.multipliers[3])) continue;
                bool valid = true;
                for (double& value : term.coefficients) valid = valid && static_cast<bool>(row >> value);
                if (valid) data.main[fileNumber - 1].push_back(term);
            } else if (fileNumber >= 10 && fileNumber <= 21) {
                PlanetaryTerm term;
                bool valid = true;
                for (int& value : term.multipliers) valid = valid && static_cast<bool>(row >> value);
                valid = valid && static_cast<bool>(row >> term.phase >> term.amplitude);
                if (valid) data.planetary[fileNumber - 1].push_back(term);
            } else {
                PerturbationTerm term;
                bool valid = static_cast<bool>(row >> term.zeta);
                for (int& value : term.lunar) valid = valid && static_cast<bool>(row >> value);
                valid = valid && static_cast<bool>(row >> term.phase >> term.amplitude);
                if (valid) data.perturbations[fileNumber - 1].push_back(term);
            }
        }
    }
    data.loaded = true;
    return true;
}

Data& data() { static Data value; if (!value.loaded) load(value); return value; }
double normalizeRadians(double value) { value = std::fmod(value, 2.0 * pi); return value < 0.0 ? value + 2.0 * pi : value; }

}  // namespace

std::optional<ELP82BPosition> calculateELP82BPosition(double jdTT) {
    Data& series = data();
    if (!series.loaded) return std::nullopt;
    const double t = (jdTT - 2451545.0) / 36525.0;
    const std::array<double, 5> powers = {1.0, t, t * t, t * t * t, t * t * t * t};
    const double deg = radiansPerDegree;
    const auto arc = [](double value) { return value / arcsecondsPerRadian; };
    std::array<double, 5> w1 = {(218.0 + 18.0 / 60.0 + 59.95571 / 3600.0) * deg, arc(1732559343.73604), arc(-5.8883), arc(0.006604), arc(-0.00003169)};
    std::array<double, 5> w2 = {(83.0 + 21.0 / 60.0 + 11.67475 / 3600.0) * deg, arc(14643420.2632), arc(-38.2776), arc(-0.045047), arc(0.00021301)};
    std::array<double, 5> w3 = {(125.0 + 2.0 / 60.0 + 40.39816 / 3600.0) * deg, arc(-6967919.3622), arc(6.3622), arc(0.007625), arc(-0.00003586)};
    std::array<double, 5> earth = {(100.0 + 27.0 / 60.0 + 59.22059 / 3600.0) * deg, arc(129597742.2758), arc(-0.0202), arc(0.000009), arc(0.00000015)};
    std::array<double, 5> peri = {(102.0 + 56.0 / 60.0 + 14.42753 / 3600.0) * deg, arc(1161.2283), arc(0.5327), arc(-0.000138), 0.0};
    std::array<std::array<double, 5>, 4> delaunay{};
    for (int k = 0; k < 5; ++k) {
        delaunay[0][k] = w1[k] - earth[k];
        delaunay[1][k] = earth[k] - peri[k];
        delaunay[2][k] = w1[k] - w2[k];
        delaunay[3][k] = w1[k] - w3[k];
    }
    delaunay[0][0] += pi;
    const std::array<double, 2> zeta = {w1[0], w1[1] + arc(5029.0966)};
    const std::array<std::array<double, 2>, 8> planets = {{
        {(252.0 + 15.0 / 60.0 + 3.25986 / 3600.0) * deg, arc(538101628.68898)},
        {(181.0 + 58.0 / 60.0 + 47.28305 / 3600.0) * deg, arc(210664136.43355)},
        {earth[0], earth[1]}, {(355.0 + 25.0 / 60.0 + 59.78866 / 3600.0) * deg, arc(68905077.59284)},
        {(34.0 + 21.0 / 60.0 + 5.34212 / 3600.0) * deg, arc(10925660.42861)},
        {(50.0 + 4.0 / 60.0 + 38.89694 / 3600.0) * deg, arc(4399609.65932)},
        {(314.0 + 3.0 / 60.0 + 18.01841 / 3600.0) * deg, arc(1542481.19393)},
        {(304.0 + 20.0 / 60.0 + 55.19575 / 3600.0) * deg, arc(786550.32074)}}};
    const double am = 0.074801329518, alpha = 0.002571881335, dtasm = 2.0 * alpha / (3.0 * am);
    const double delnu = arc(0.55604) / w1[1], dele = arc(0.01789), delg = arc(-0.08066), delnp = arc(-0.06424) / w1[1], delep = arc(-0.12879);
    std::array<double, 3> coordinate{};
    for (int component = 0; component < 3; ++component) {
        for (const MainTerm& term : series.main[component]) {
            double amplitude = term.coefficients[0];
            const double tgv = term.coefficients[1] + dtasm * term.coefficients[5];
            if (component == 2) amplitude -= 2.0 * amplitude * delnu / 3.0;
            amplitude += tgv * (delnp - am * delnu) + term.coefficients[2] * delg + term.coefficients[3] * dele + term.coefficients[4] * delep;
            double argument = 0.0;
            for (int k = 0; k < 5; ++k) for (int index = 0; index < 4; ++index) argument += term.multipliers[index] * delaunay[index][k] * powers[k];
            if (component == 2) argument += pi / 2.0;
            coordinate[component] += amplitude * std::sin(argument);
        }
    }
    for (int fileNumber = 4; fileNumber <= 36; ++fileNumber) {
        const int component = (fileNumber - 1) % 3;
        const bool planetary = fileNumber >= 10 && fileNumber <= 21;
        const int table = (fileNumber + 2) / 3;
        const double factor = table == 3 || table == 5 || table == 7 || table == 9 ? t : table == 12 ? t * t : 1.0;
        if (planetary) {
            for (const PlanetaryTerm& term : series.planetary[fileNumber - 1]) {
                double argument = term.phase * deg;
                if (fileNumber < 16) {
                    argument += term.multipliers[8] * (delaunay[0][0] + delaunay[0][1] * t) + term.multipliers[9] * (delaunay[2][0] + delaunay[2][1] * t) + term.multipliers[10] * (delaunay[3][0] + delaunay[3][1] * t);
                    for (int index = 0; index < 8; ++index) argument += term.multipliers[index] * (planets[index][0] + planets[index][1] * t);
                } else {
                    for (int index = 0; index < 4; ++index) argument += term.multipliers[index + 7] * (delaunay[index][0] + delaunay[index][1] * t);
                    for (int index = 0; index < 7; ++index) argument += term.multipliers[index] * (planets[index][0] + planets[index][1] * t);
                }
                coordinate[component] += factor * term.amplitude * std::sin(argument);
            }
        } else {
            for (const PerturbationTerm& term : series.perturbations[fileNumber - 1]) {
                double argument = term.phase * deg + term.zeta * (zeta[0] + zeta[1] * t);
                for (int index = 0; index < 4; ++index) argument += term.lunar[index] * (delaunay[index][0] + delaunay[index][1] * t);
                coordinate[component] += factor * term.amplitude * std::sin(argument);
            }
        }
    }
    double longitude = 0.0;
    for (int k = 0; k < 5; ++k) longitude += w1[k] * powers[k];
    longitude = normalizeRadians(longitude + coordinate[0] / arcsecondsPerRadian);
    const double latitude = coordinate[1] / arcsecondsPerRadian;
    const double distance = coordinate[2] * kilometersScale;
    return ELP82BPosition{longitude / deg, latitude / deg, distance};
}
