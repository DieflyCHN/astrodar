#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include "solar_model.hpp"
#include "solar_events.hpp"
#include "solar_time.hpp"
#include "moon_model.hpp"
#include "localization.hpp"
#include "observer.hpp"
#include "solar_terms.hpp"
#include "time_scales.hpp"
#include "time_zone_reference.hpp"
#include "location_config.hpp"

constexpr int REPUBLIC_OFFSET = 841;

LocationPanel timeZonePanel(const std::string& id, const std::string& timeZone, const double fallbackLatitude, const double fallbackLongitude) {
    const auto reference = findTimeZoneReference(timeZone);
    return {LocationPanelKind::TimeZone, id, timeZone,
            reference ? reference->latitudeDegrees : fallbackLatitude,
            reference ? reference->longitudeDegrees : fallbackLongitude};
}

const LocationPanel BERLIN_PANEL = timeZonePanel("BER", "Europe/Berlin", 52.5200, 13.4050);
const LocationPanel BEIJING_PANEL = timeZonePanel("BEI", "Asia/Shanghai", 39.9042, 116.4074);
const ObserverLocation BERLIN{"BER", "Europe/Berlin", 52.5200, 13.4050, 34.0};
const ObserverLocation BEIJING{"BEI", "Asia/Shanghai", 39.9042, 116.4074, 43.5};

std::optional<double> parseCoordinate(const std::string& input, double maximumDegrees) {
    std::string normalized = input;
    const auto replaceAll = [&](const std::string& token) {
        std::size_t position = 0;
        while ((position = normalized.find(token, position)) != std::string::npos) {
            normalized.replace(position, token.size(), " ");
        }
    };
    for (const std::string& token : {"°", "′", "″", "'", "\"", "d", "D", "m", "M", "s", "S"}) replaceAll(token);
    std::istringstream stream(normalized);
    std::vector<double> values;
    double value = 0.0;
    while (stream >> value) values.push_back(value);
    if (values.empty() || values.size() == 2 || values.size() > 3) return std::nullopt;
    double decimal = values[0];
    if (values.size() == 3) {
        if (values[0] < 0.0 || values[1] < 0.0 || values[1] >= 60.0 || values[2] < 0.0 || values[2] >= 60.0) return std::nullopt;
        decimal += values[1] / 60.0 + values[2] / 3600.0;
    }
    if (decimal < 0.0 || decimal > maximumDegrees) return std::nullopt;
    return decimal;
}

std::optional<std::time_t> parseUtcDateTime(const std::string& input) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(input.c_str(), "%d%*[-./]%d%*[-./]%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return std::nullopt;
    }
    const bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int daysInMonth[] = {31, leapYear ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12 || day < 1 || day > daysInMonth[month - 1] || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return std::nullopt;
    }
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    const int jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    const double jd = static_cast<double>(jdn) - 0.5 +
        (hour * 3600.0 + minute * 60.0 + second) / 86400.0;
    return static_cast<std::time_t>(std::llround((jd - 2440587.5) * 86400.0));
}

struct EditableUtc { int year = 2026, month = 6, day = 23, hour = 12, minute = 0, second = 0; };

int daysInMonth(int year, int month) {
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int days[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1];
}

void adjustEditableUtc(EditableUtc& value, int segment, int delta) {
    if (segment == 0) value.year = std::clamp(value.year + delta, 1, 9999);
    else if (segment == 1) {
        value.month += delta;
        if (value.month < 1) {
            if (value.year == 1) value.month = 1;
            else { value.month = 12; --value.year; }
        }
        if (value.month > 12) {
            if (value.year == 9999) value.month = 12;
            else { value.month = 1; ++value.year; }
        }
    }
    else if (segment == 2) {
        value.day += delta;
        if (value.day < 1) {
            if (value.year == 1 && value.month == 1) value.day = 1;
            else {
                if (--value.month < 1) { value.month = 12; --value.year; }
                value.day = daysInMonth(value.year, value.month);
            }
        }
        if (value.day > daysInMonth(value.year, value.month)) {
            if (value.year == 9999 && value.month == 12) value.day = 31;
            else {
                value.day = 1;
                if (++value.month > 12) { value.month = 1; ++value.year; }
            }
        }
    }
    else {
        const int unit = segment == 3 ? 3600 : segment == 4 ? 60 : 1;
        int seconds = value.hour * 3600 + value.minute * 60 + value.second + delta * unit;
        while (seconds < 0) { seconds += 86400; adjustEditableUtc(value, 2, -1); }
        while (seconds >= 86400) { seconds -= 86400; adjustEditableUtc(value, 2, 1); }
        value.hour = seconds / 3600;
        value.minute = seconds % 3600 / 60;
        value.second = seconds % 60;
    }
    value.day = std::min(value.day, daysInMonth(value.year, value.month));
}

std::string formatEditableUtc(const EditableUtc& value) {
    std::ostringstream out; out << std::setfill('0') << std::setw(4) << value.year << '-' << std::setw(2) << value.month << '-' << std::setw(2) << value.day << ' ' << std::setw(2) << value.hour << ':' << std::setw(2) << value.minute << ':' << std::setw(2) << value.second; return out.str();
}

std::string formatCoordinates(double latitude, double longitude) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << std::abs(latitude) << (latitude >= 0.0 ? " N" : " S")
        << "，" << std::abs(longitude) << (longitude >= 0.0 ? " E" : " W");
    return out.str();
}

std::string asciiLower(std::string value) {
    for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

std::vector<std::string> filterTimeZones(const std::vector<std::string>& zones, const std::string& filter) {
    const std::string normalizedFilter = asciiLower(filter);
    std::vector<std::string> matches;
    for (const std::string& zone : zones) {
        if (asciiLower(zone).find(normalizedFilter) != std::string::npos) matches.push_back(zone);
    }
    return matches;
}

int gregorianToRepublicanYear(int gregorianYear) {
    return gregorianYear + REPUBLIC_OFFSET;
}

struct GregorianTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
};

struct Ganzhi {
    int index;
    const char* gan;
    const char* zhi;
};

struct ChineseHour {
    const char* hourGan;
    const char* hourZhi;
    const char* chuZheng;
    const char* ke;
};

const char* WEEKDAY_STR_CHN[] = {
    "星期日", "星期一", "星期二", "星期三",
    "星期四", "星期五", "星期六"
};

const char* TIANGAN[] = {
    "甲", "乙", "丙", "丁", "戊",
    "己", "庚", "辛", "壬", "癸"
};

const char* DIZHI[] = {
    "子", "丑", "寅", "卯", "辰", "巳",
    "午", "未", "申", "酉", "戌", "亥"
};

const char* KE[] = {"初", "一", "二", "三"};

GregorianTime tmToGregorian(const tm& t) {
    GregorianTime g{};
    g.year = t.tm_year + 1900;
    g.month = t.tm_mon + 1;
    g.day = t.tm_mday;
    g.hour = t.tm_hour;
    g.minute = t.tm_min;
    g.second = t.tm_sec;
    g.weekday = t.tm_wday;
    return g;
}

GregorianTime unixToGregorianUTC(std::time_t unixUTC) {
    tm t{};
    gmtime_r(&unixUTC, &t);
    return tmToGregorian(t);
}

// POSIX does not provide a per-call time-zone conversion API.  Serializing
// changes to TZ keeps this implementation safe when the program is extended
// with worker threads, and works on both Linux and macOS.
GregorianTime unixToGregorianInTimezone(std::time_t unixUTC, const char* tzName) {
    static std::mutex timezoneMutex;
    std::lock_guard<std::mutex> lock(timezoneMutex);

    const char* oldTZ = std::getenv("TZ");
    std::string savedTZ = oldTZ ? oldTZ : "";
    bool hadTZ = oldTZ != nullptr;

    setenv("TZ", tzName, 1);
    tzset();

    tm t{};
    localtime_r(&unixUTC, &t);

    if (hadTZ) {
        setenv("TZ", savedTZ.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return tmToGregorian(t);
}

double unixToJD(std::time_t unixUTC) {
    return static_cast<double>(unixUTC) / 86400.0 + 2440587.5;
}

std::int64_t unixToJDN(std::time_t unixUTC) {
    return static_cast<std::int64_t>(std::floor(unixToJD(unixUTC) + 0.5));
}

std::int64_t gregorianToJDN(const GregorianTime& g) {
    int a = (14 - g.month) / 12;
    int y = g.year + 4800 - a;
    int m = g.month + 12 * a - 3;
    return g.day + (153 * m + 2) / 5 + 365LL * y + y / 4 - y / 100 + y / 400 - 32045;
}

Ganzhi jdnToDayGanzhi(std::int64_t jdn) {
    int index = static_cast<int>((jdn + 49) % 60);
    if (index < 0) {
        index += 60;
    }

    Ganzhi gz{};
    gz.index = index;
    gz.gan = TIANGAN[index % 10];
    gz.zhi = DIZHI[index % 12];
    return gz;
}

Ganzhi gregorianYearToGanzhi(int gregorianYear) {
    int index = (gregorianYear - 4) % 60;
    if (index < 0) {
        index += 60;
    }

    return {index, TIANGAN[index % 10], DIZHI[index % 12]};
}

Ganzhi ganzhiYearAt(const GregorianTime& localTime, double solarLongitude) {
    // 八字年以立春（太阳视黄经 315°）为界。这里将“先查立春时刻再
    // 比较”的传统表述化简为本地公历年的直接判断：一月必在立春前；
    // 二月仅在 λ < 315° 时仍属上一年。规则本身没有改变。
    const bool beforeLichun = localTime.month == 1 ||
        (localTime.month == 2 && solarLongitude < 315.0);
    const int ganzhiYear = localTime.year - (beforeLichun ? 1 : 0);
    return gregorianYearToGanzhi(ganzhiYear);
}

Ganzhi ganzhiMonthAt(const Ganzhi& yearGanzhi, double solarLongitude) {
    // 十二个八字月的边界是 λ = 315° + 30°m，而非农历月或中气。
    // 因而传统“寅月起、逐月顺推”的口诀可等价地写为以下模运算：
    // m=0 是寅月，m=11 是丑月；月干从该年干对应的寅月干顺推。
    double offset = std::fmod(solarLongitude - 315.0 + 360.0, 360.0);
    if (offset < 0.0) {
        offset += 360.0;
    }
    const int monthOrdinal = static_cast<int>(std::floor(offset / 30.0));
    const int ganIndex = (2 + 2 * (yearGanzhi.index % 10 % 5) + monthOrdinal) % 10;
    const int zhiIndex = (2 + monthOrdinal) % 12;
    return {ganIndex, TIANGAN[ganIndex], DIZHI[zhiIndex]};
}

ChineseHour dayGanzhiToHourGanzhi(const Ganzhi& dayGanzhi, int hour, int minute) {
    int zhiIndex = ((hour + 1) / 2) % 12;
    int ganIndex = ((dayGanzhi.index % 5) * 2 + zhiIndex) % 10;

    ChineseHour ch{};
    ch.hourGan = TIANGAN[ganIndex];
    ch.hourZhi = DIZHI[zhiIndex];
    ch.chuZheng = (hour % 2 == 0) ? "正" : "初";
    ch.ke = KE[minute / 15];
    return ch;
}

void printGregorianLine(const char* label, const GregorianTime& g) {
    std::cout
        << label << ':'
        << std::setfill('0')
        << std::setw(4) << g.year << '.'
        << std::setw(2) << g.month << '.'
        << std::setw(2) << g.day << "  "
        << std::setw(2) << g.hour << ':'
        << std::setw(2) << g.minute << ':'
        << std::setw(2) << g.second << "  "
        << WEEKDAY_STR_CHN[g.weekday]
        << '\n';
}

std::string formatGregorianTime(const GregorianTime& g) {
    std::ostringstream output;
    output << std::setfill('0')
           << std::setw(4) << g.year << '.'
           << std::setw(2) << g.month << '.'
           << std::setw(2) << g.day << ' '
           << std::setw(2) << g.hour << ':'
           << std::setw(2) << g.minute << ':'
           << std::setw(2) << g.second;
    return output.str();
}

void printSolarTermEvent(const char* label, const SolarTermEvent& event) {
    std::cout << label << event.chineseName << "  λ=" << event.longitudeDegrees << "°  ";
    const auto jdUTC = ttJulianDateToUTC(event.jdTT);
    if (!jdUTC) {
        std::cout << "UTC unavailable\n";
        return;
    }

    const std::time_t unixUTC = static_cast<std::time_t>(std::llround(
        (*jdUTC - 2440587.5) * 86400.0));
    std::cout << formatGregorianTime(unixToGregorianUTC(unixUTC)) << " UTC\n";
}

void printGanzhiLine(const Ganzhi& yearGanzhi, const Ganzhi& monthGanzhi, const Ganzhi& dayGanzhi,
                     const ChineseHour& hourGanzhi) {
    std::cout
        << "干支: " << yearGanzhi.gan << yearGanzhi.zhi << "年 "
        << monthGanzhi.gan << monthGanzhi.zhi << "月 "
        << dayGanzhi.gan << dayGanzhi.zhi << "日 "
        << hourGanzhi.hourGan << hourGanzhi.hourZhi
        << hourGanzhi.chuZheng << hourGanzhi.ke << "刻\n";
}

int main() {
    using namespace ftxui;
    const auto tr = [](std::string_view key) { return std::string(localization::text(key)); };
    ScreenInteractive screen = ScreenInteractive::Fullscreen();
    int page = 0;
    bool showGlossary = false;
    bool showQuery = false;
    bool showAddLocation = false;
    bool showEditLocation = false;
    int editLocationIndex = 0;
    bool editLocationSelected = false;
    bool confirmDeleteLocation = false;
    std::string editLocationStatus;
    int addLocationField = 0;
    int addLocationKind = 0;  // 0: IANA 时区，1: 自定义坐标。
    std::string addTimeZone = "Europe/Berlin";
    std::string addTimeZoneFilter;
    bool addTimeZoneSelecting = false;
    int addTimeZoneSelection = 0;
    std::string addLocationStatus;
    std::string addCoordinateName = "自定义地点";
    std::string addLatitude = "52.5200";
    std::string addLongitude = "13.4050";
    bool addNorth = true;
    bool addEast = true;
    int queryField = 0;
    std::string queryLatitude = "52.5200";
    std::string queryLongitude = "13.4050";
    bool queryNorth = true;
    bool queryEast = true;
    int queryTimeSource = 0;
    EditableUtc queryCustomUtc;
    bool queryTimeEditing = false;
    int queryTimeSegment = 0;
    std::string queryJulianDate = "2461215.000000";
    std::atomic<bool> running{true};
    std::atomic<int> refreshMilliseconds{1000};
    const std::vector<std::string> ianaTimeZones = listIanaTimeZones();
    std::vector<LocationPanel> panels = loadLocationPanels();
    if (panels.empty()) panels = {BERLIN_PANEL, BEIJING_PANEL};
    for (LocationPanel& panel : panels) {
        if (panel.kind != LocationPanelKind::TimeZone) continue;
        if (const auto reference = findTimeZoneReference(panel.timeZone)) {
            panel.latitudeDegrees = reference->latitudeDegrees;
            panel.longitudeDegrees = reference->longitudeDegrees;
        }
    }
    if (panels.size() > 4) panels.resize(4);

    auto renderer = Renderer([&] {
        const std::time_t nowUTC = std::time(nullptr);
        const GregorianTime utc = unixToGregorianUTC(nowUTC);
        const double jd = unixToJD(nowUTC);
        const double mjd = jd - 2400000.5;
        const auto jdTT = utcJulianDateToTT(jd);
        if (!jdTT) return text("UTC-to-TT conversion unavailable") | border;

        const SolarPosition sun = calculateSolarPosition(*jdTT);
        const MoonPosition moon = calculateMoonPosition(*jdTT, sun.apparentEclipticLongitude);
        const SolarTermEvent current = currentSolarTerm(*jdTT);
        const SolarTermEvent next = nextSolarTerm(*jdTT);
        const auto city = [&](const LocationPanel& panel, const GregorianTime& time) {
            const Ganzhi day = jdnToDayGanzhi(gregorianToJDN(time));
            const Ganzhi year = ganzhiYearAt(time, sun.apparentEclipticLongitude);
            const Ganzhi month = ganzhiMonthAt(year, sun.apparentEclipticLongitude);
            const ChineseHour hour = dayGanzhiToHourGanzhi(day, time.hour, time.minute);
            const std::string coordinateLabel = panel.kind == LocationPanelKind::TimeZone ? "参考坐标：" : "坐标：";
            return vbox({text(panel.id) | bold,
                         text(coordinateLabel + formatCoordinates(panel.latitudeDegrees, panel.longitudeDegrees)),
                         text(formatGregorianTime(time) + "  " + hour.hourZhi + hour.chuZheng + hour.ke + "刻"),
                         text(std::string("干支: ") + year.gan + year.zhi + "年 " + month.gan + month.zhi + "月 " +
                              day.gan + day.zhi + "日 " + hour.hourGan + hour.hourZhi + "时")});
        };
        const auto panelTime = [&](const LocationPanel& panel, const std::time_t instant) {
            if (panel.kind == LocationPanelKind::TimeZone) {
                return unixToGregorianInTimezone(instant, panel.timeZone.c_str());
            }
            const LocalSolarTime local = calculateLocalSolarTime(instant, panel.longitudeDegrees, sun.equationOfTimeMinutes);
            return GregorianTime{local.year, local.month, local.day, local.hour, local.minute, local.second, local.weekday};
        };
        const auto eventTime = [&](const LocationPanel& panel, const std::optional<std::time_t>& event) {
            if (!event) return std::string("--:--");
            if (panel.kind == LocationPanelKind::Coordinates) {
                const auto eventTT = utcJulianDateToTT(unixToJD(*event));
                const double eventEot = eventTT ? calculateSolarPosition(*eventTT).equationOfTimeMinutes : sun.equationOfTimeMinutes;
                const LocalSolarTime local = calculateLocalSolarTime(*event, panel.longitudeDegrees, eventEot);
                std::ostringstream out;
                out << std::setfill('0') << std::setw(2) << local.hour << ':' << std::setw(2) << local.minute;
                return out.str();
            }
            const GregorianTime localEvent = unixToGregorianInTimezone(*event, panel.timeZone.c_str());
            return formatGregorianTime(localEvent).substr(11);
        };
        const auto daylightDuration = [](const SolarEvents& events) {
            if (!events.sunrise || !events.sunset) return std::string("--:--");
            const long minutes = static_cast<long>((*events.sunset - *events.sunrise) / 60);
            std::ostringstream output;
            output << std::setfill('0') << std::setw(2) << minutes / 60 << ':' << std::setw(2) << minutes % 60;
            return output.str();
        };
        const auto eventTimeline = [&](const LocationPanel& panel, const SolarEvents& events) {
            Elements morning = {text("天文曙光  " + eventTime(panel, events.astronomicalDawn)),
                text("航海曙光  " + eventTime(panel, events.nauticalDawn)),
                text("民用曙光  " + eventTime(panel, events.civilDawn)),
                text("日出      " + eventTime(panel, events.sunrise)),
                text("中天      " + eventTime(panel, events.solarNoon))};
            Elements evening = {text("日落      " + eventTime(panel, events.sunset)),
                text("民用暮光  " + eventTime(panel, events.civilDusk)),
                text("航海暮光  " + eventTime(panel, events.nauticalDusk)),
                text("天文暮光  " + eventTime(panel, events.astronomicalDusk)),
                separator(), text(tr("daylight") + "  " + daylightDuration(events))};
            // 四宫格完整显示约需 30 行；矮终端中改为两列以保留所有时间点。
            if (Terminal::Size().dimy < 32) {
                return hbox({vbox(std::move(morning)) | xflex_grow,
                             vbox(std::move(evening)) | xflex_grow});
            }
            morning.insert(morning.end(), std::make_move_iterator(evening.begin()), std::make_move_iterator(evening.end()));
            return vbox(std::move(morning));
        };
        const auto panelCard = [&](const LocationPanel& panel) {
            const GregorianTime time = panelTime(panel, nowUTC);
            const ObserverLocation observer{panel.id, panel.timeZone, panel.latitudeDegrees, panel.longitudeDegrees, 0.0};
            const SolarEvents events = calculateSolarEvents(observer, time.year, time.month, time.day);
            return vbox({city(panel, time), separator(), eventTimeline(panel, events)}) | border | xflex_grow;
        };
        const auto termLine = [&](const SolarTermEvent& event) {
            const auto jdUTC = ttJulianDateToUTC(event.jdTT);
            if (!jdUTC) return std::string(event.chineseName) + "  UTC unavailable";
            const auto stamp = static_cast<std::time_t>(std::llround((*jdUTC - 2440587.5) * 86400.0));
            return std::string(event.chineseName) + "  " + formatGregorianTime(unixToGregorianUTC(stamp)) + " UTC";
        };
        const auto moonPhaseName = [&] {
            const double angle = moon.signedElongation;
            const double magnitude = std::abs(angle);
            if (magnitude < 20.0) return std::string("朔月");
            if (magnitude > 160.0) return std::string("望月");
            if (angle > 0.0) return magnitude < 90.0 ? std::string("渐盈眉月") : std::string("渐盈凸月");
            return magnitude < 90.0 ? std::string("渐亏残月") : std::string("渐亏凸月");
        };
        const auto moonDisk = [&] {
            Elements rows;
            constexpr int rowRadius = 6;
            // 先在等比例逻辑网格中求圆；最后才按字符数量横向扩展，避免
            // 字体纵横比影响天文相位与圆形轮廓的基础计算。
            constexpr double horizontalCharacterScale = 2.2;
            const auto oddWidth = [](int width) { return width % 2 == 0 ? width + 1 : width; };
            const int maximumWidth = oddWidth(static_cast<int>(std::round((2 * rowRadius + 1) * horizontalCharacterScale)));
            const double phase = moon.signedElongation * 3.14159265358979323846 / 180.0;
            const double sunX = std::sin(phase);
            const double sunZ = -std::cos(phase);
            for (int y = -rowRadius; y <= rowRadius; ++y) {
                const double halfWidth = std::sqrt(rowRadius * rowRadius - y * y);
                const double continuousWidth = 2.0 * halfWidth + 1.0;
                // 连续圆形宽度先乘显示倍率，最后仅在字符输出边界取整一次。
                // 奇数宽度使每一行共享同一个中心字符列，避免偶数宽度的半格偏移。
                int renderedWidth = oddWidth(std::max(1, static_cast<int>(std::round(continuousWidth * horizontalCharacterScale))));
                // 首末行在终端栅格中容易显得过尖，额外扩展三个字符作视觉补偿。
                if (std::abs(y) == rowRadius) {
                    renderedWidth += 3;
                }
                std::string row((maximumWidth - renderedWidth) / 2, ' ');
                for (int column = 0; column < renderedWidth; ++column) {
                    const double x = halfWidth == 0.0 ? 0.0 :
                        -halfWidth + (column + 0.5) * (2.0 * halfWidth / renderedWidth);
                    const double radial = x * x + y * y;
                    const double illumination = x / rowRadius * sunX +
                        std::sqrt(1.0 - radial / (rowRadius * rowRadius)) * sunZ;
                    row += illumination >= 0.0 ? "▓" : "░";
                }
                rows.push_back(text(row));
            }
            return vbox(std::move(rows));
        };
        Element content;
        if (page == 0) {
            Elements cards;
            for (const LocationPanel& panel : panels) cards.push_back(panelCard(panel));
            // 无论卡片数为多少都维持 2×2 栅格；空槽防止少量卡片拉伸为整页。
            std::vector<Elements> rows(2);
            for (int index = 0; index < 4; ++index) {
                rows[index / 2].push_back(index < static_cast<int>(cards.size()) ? cards[index] : filler());
            }
            content = gridbox(std::move(rows));
        } else {
            Elements data;
            data.push_back(text(tr("astronomical_data")) | bold);
            data.push_back(text(tr("utc") + ": " + formatGregorianTime(utc) + " UTC"));
            data.push_back(text(tr("unix_time") + ": " + std::to_string(static_cast<long long>(nowUTC))));
            data.push_back(text(tr("jd") + ": " + std::to_string(jd)));
            data.push_back(text(tr("mjd") + ": " + std::to_string(mjd)));
            data.push_back(text(tr("jd_tt") + " (TT): " + std::to_string(*jdTT)));
            data.push_back(separator());
            Elements solarData = {text(tr("sun")) | bold,
                text(tr("sun_longitude") + ": " + std::to_string(sun.apparentEclipticLongitude) + "°"),
                text(tr("sun_latitude") + ": " + std::to_string(sun.eclipticLatitude) + "°"),
                text(tr("sun_declination") + ": " + std::to_string(sun.declination) + "°"),
                text(tr("sun_distance") + ": " + std::to_string(sun.distanceAU) + " AU")};
            Elements moonData = {text(tr("moon")) | bold,
                text(tr("moon_longitude") + ": " + std::to_string(moon.eclipticLongitude) + "°"),
                text(tr("moon_latitude") + ": " + std::to_string(moon.eclipticLatitude) + "°"),
                text(tr("moon_distance") + ": " + std::to_string(moon.distanceKm) + " km"),
                text(tr("moon_declination") + ": " + std::to_string(moon.declination) + "°"),
                text(tr("moon_elongation") + ": " + std::to_string(moon.elongation) + "°"),
                text(tr("moon_illumination") + ": " + std::to_string(moon.illuminatedFraction * 100.0) + "%")};
            data.push_back(hbox({vbox({vbox(std::move(solarData)) | border,
                                        vbox(std::move(moonData)) | border}) | flex,
                                 vbox({text(tr("lunar_phase")) | bold | center, moonDisk() | center,
                                       text(moonPhaseName()) | center,
                                       text(tr("illumination") + ": " + std::to_string(moon.illuminatedFraction * 100.0) + "%") | center}) | border}));
            data.push_back(separator());
            data.push_back(text(tr("current_term") + ": " + termLine(current)));
            data.push_back(text(tr("next_term") + ": " + termLine(next)));
            data.push_back(text(tr("major_terms") + ":") | bold);
            constexpr int longitudes[] = {315, 0, 45, 90, 135, 180, 225, 270};
            Elements leftTerms;
            Elements rightTerms;
            for (int index = 0; index < 8; ++index) {
                const Element item = text("  " + termLine(solarTermInGregorianYear(utc.year, longitudes[index])));
                (index < 4 ? leftTerms : rightTerms).push_back(item);
            }
            data.push_back(hbox({vbox(std::move(leftTerms)) | flex, vbox(std::move(rightTerms)) | flex}));
            content = vbox(std::move(data));
        }
        const Element title = hbox({text("astrodar  ·  " + (page == 0 ? tr("overview") : tr("astronomical_data"))) | bold | xflex,
                                    text(tr("refresh") + ": " + std::to_string(refreshMilliseconds.load()) + " ms  [-/+]" ) | dim});
        const std::string footer = page == 0
            ? "[A] 添加地点  [E] 编辑地点  " + tr("help")
            : tr("help");
        const Element base = vbox({title,
                     separator(), content | yflex,
                     separator(), text(footer) | dim | size(HEIGHT, EQUAL, 1) | notflex}) | border;
        if (showEditLocation) {
            Elements rows = {text("编辑地点") | bold | center, separator()};
            if (panels.empty()) {
                rows.push_back(text("没有已保存的地点。") | dim);
            } else {
                for (std::size_t index = 0; index < panels.size(); ++index) {
                    const std::string marker = static_cast<int>(index) == editLocationIndex
                        ? (editLocationSelected ? "◆ " : "▶ ") : "  ";
                    rows.push_back(text(marker + panels[index].id));
                }
            }
            rows.push_back(separator());
            if (!editLocationStatus.empty()) rows.push_back(text(editLocationStatus) | dim);
            rows.push_back(text(editLocationSelected
                ? "[↑↓] 排序  [Enter] 取消选中  [退格] 删除"
                : "[↑↓] 选择地点  [Enter] 选中") | dim | center);
            rows.push_back(text("[Esc] 关闭") | dim | center);
            const Element dialog = vbox(std::move(rows)) | border | size(WIDTH, LESS_THAN, 60) | center;
            if (confirmDeleteLocation) {
                const Element confirmation = vbox({text("确认删除地点？") | bold | center,
                    text(panels.empty() ? "" : panels[editLocationIndex].id) | center,
                    separator(), text("[Enter] 删除  [Esc] 取消") | dim | center})
                    | border | size(WIDTH, LESS_THAN, 36) | center;
                return dbox({base, dialog | dim, confirmation | clear_under});
            }
            return dbox({base, dialog | clear_under});
        }
        if (showAddLocation) {
            const auto addField = [&](int index, const std::string& label, const std::string& value) {
                return text(std::string(index == addLocationField ? "▶ " : "  ") + label + value);
            };
            const bool isTimeZone = addLocationKind == 0;
            const std::vector<std::string> timeZoneMatches = filterTimeZones(ianaTimeZones, addTimeZoneFilter);
            Elements dialogRows = {text("添加地点") | bold | center, separator(),
                addField(0, "地点类型：", isTimeZone ? "时区" : "坐标")};
            if (isTimeZone) {
                dialogRows.push_back(addField(1, "IANA 时区：", addTimeZone));
                if (addTimeZoneSelecting) {
                    dialogRows.push_back(separator());
                    dialogRows.push_back(text("筛选：" + addTimeZoneFilter) | underlined);
                    if (timeZoneMatches.empty()) {
                        dialogRows.push_back(text("没有匹配的 IANA 时区") | dim);
                    } else {
                        const int first = std::max(0, std::min(addTimeZoneSelection - 3,
                            static_cast<int>(timeZoneMatches.size()) - 8));
                        const int last = std::min(first + 8, static_cast<int>(timeZoneMatches.size()));
                        for (int index = first; index < last; ++index) {
                            dialogRows.push_back(text(std::string(index == addTimeZoneSelection ? "▶ " : "  ") + timeZoneMatches[index]));
                        }
                        dialogRows.push_back(text(std::to_string(addTimeZoneSelection + 1) + "/" + std::to_string(timeZoneMatches.size())) | dim);
                    }
                }
            } else {
                dialogRows.push_back(addField(1, "名称：", addCoordinateName));
                dialogRows.push_back(addField(2, "纬度：", addLatitude + (addNorth ? " N" : " S")));
                dialogRows.push_back(addField(3, "经度：", addLongitude + (addEast ? " E" : " W")));
            }
            dialogRows.push_back(addField(isTimeZone ? 2 : 4, "确认：", "添加地点"));
            dialogRows.push_back(separator());
            dialogRows.push_back(text(isTimeZone
                ? "时区地点将采用 IANA 时区数据库的参考坐标计算太阳活动。"
                : "自定义地点不保存时区，所有时间按真太阳时计算。"));
            dialogRows.push_back(text("确认后将保存配置；概览最多显示四个地点。") | dim);
            if (!addLocationStatus.empty()) dialogRows.push_back(text(addLocationStatus) | dim);
            dialogRows.push_back(separator());
            dialogRows.push_back(text(addTimeZoneSelecting
                ? "[↑↓] 选择  [输入/退格] 筛选  [Enter] 确认  [Esc] 返回"
                : "[↑↓] 选择  [Tab] 切换该行模式  [Enter] 选择  [Esc] 关闭") | dim | center);
            const Element dialog = vbox(std::move(dialogRows)) | border | size(WIDTH, LESS_THAN, 78) | center;
            return dbox({base, dialog | clear_under});
        }
        if (showQuery) {
            const auto latitudeValue = parseCoordinate(queryLatitude, 90.0);
            const auto longitudeValue = parseCoordinate(queryLongitude, 180.0);
            const std::string customUtcText = formatEditableUtc(queryCustomUtc);
            const auto customUtc = parseUtcDateTime(customUtcText);
            const auto parsedJD = [&]() -> std::optional<double> {
                try { const double value = std::stod(queryJulianDate); return value > 2400000.0 ? std::optional<double>{value} : std::nullopt; }
                catch (...) { return std::nullopt; }
            }();
            const double queryJD = queryTimeSource == 2 && parsedJD ? *parsedJD : unixToJD(queryTimeSource == 1 && customUtc ? *customUtc : nowUTC);
            const auto queryTT = queryTimeSource == 2 ? parsedJD : utcJulianDateToTT(queryJD);
            std::optional<std::time_t> queryUtcValue;
            if (queryTimeSource == 2 && queryTT) {
                const auto jdUTC = ttJulianDateToUTC(*queryTT);
                if (jdUTC) queryUtcValue = static_cast<std::time_t>(std::llround((*jdUTC - 2440587.5) * 86400.0));
            } else {
                queryUtcValue = static_cast<std::time_t>(std::llround((queryJD - 2440587.5) * 86400.0));
            }
            const std::time_t queryUtc = queryUtcValue.value_or(nowUTC);
            std::string inputStatus = "输入有效";
            if (!latitudeValue) inputStatus = "纬度格式或范围错误（应为 0–90°）";
            else if (!longitudeValue) inputStatus = "经度格式或范围错误（应为 0–180°）";
            else if (queryTimeSource == 1 && !customUtc) inputStatus = "自定义 UTC 时间格式错误（YYYY-MM-DD HH:MM:SS）";
            else if (queryTimeSource == 2 && !parsedJD) inputStatus = "JD(TT) 不合法（应为大于 2400000 的数字）";
            else if (!queryTT) inputStatus = "UTC 时间不支持 TT 转换（早于 1972 年）";
            else if (!queryUtcValue) inputStatus = "JD(TT) 无法转换为当前支持范围的 UTC 时间";
            const double latitude = (queryNorth ? 1.0 : -1.0) * latitudeValue.value_or(0.0);
            const double longitude = (queryEast ? 1.0 : -1.0) * longitudeValue.value_or(0.0);
            const SolarPosition querySun = queryTT ? calculateSolarPosition(*queryTT) : sun;
            const MoonPosition queryMoon = queryTT ? calculateMoonPosition(*queryTT, querySun.apparentEclipticLongitude) : moon;
            const LocalSolarTime solarTime = calculateLocalSolarTime(queryUtc, longitude, querySun.equationOfTimeMinutes);
            const GregorianTime queryTime{solarTime.year, solarTime.month, solarTime.day, solarTime.hour, solarTime.minute, solarTime.second, solarTime.weekday};
            const Ganzhi queryDay = jdnToDayGanzhi(gregorianToJDN(queryTime));
            const Ganzhi queryYear = ganzhiYearAt(queryTime, querySun.apparentEclipticLongitude);
            const Ganzhi queryMonth = ganzhiMonthAt(queryYear, querySun.apparentEclipticLongitude);
            const ChineseHour queryHour = dayGanzhiToHourGanzhi(queryDay, queryTime.hour, queryTime.minute);
            const ObserverLocation queryLocation{"查询", "UTC", latitude, longitude, 0.0};
            const SolarEvents queryEvents = calculateSolarEvents(queryLocation, queryTime.year, queryTime.month, queryTime.day);
            const auto qevent = [&](const std::optional<std::time_t>& event) {
                if (!event) return std::string("--:--");
                const auto eventTT = utcJulianDateToTT(unixToJD(*event));
                const double eventEot = eventTT ? calculateSolarPosition(*eventTT).equationOfTimeMinutes : querySun.equationOfTimeMinutes;
                const LocalSolarTime time = calculateLocalSolarTime(*event, longitude, eventEot);
                std::ostringstream out; out << std::setfill('0') << std::setw(2) << time.hour << ':' << std::setw(2) << time.minute; return out.str();
            };
            const auto qdaylight = [&] {
                if (!queryEvents.sunrise || !queryEvents.sunset) return std::string("--:--");
                const long minutes = static_cast<long>((*queryEvents.sunset - *queryEvents.sunrise) / 60);
                std::ostringstream out; out << std::setfill('0') << std::setw(2) << minutes / 60 << ':' << std::setw(2) << minutes % 60; return out.str();
            };
            const auto field = [&](int index, const std::string& label, const std::string& value) {
                return text(std::string(index == queryField ? "▶ " : "  ") + label + value);
            };
            const std::string sourceLabel = queryTimeSource == 0 ? "实时 UTC" : queryTimeSource == 1 ? "自定义 UTC" : "JD(TT)";
            const std::string activeTimeLabel = queryTimeSource == 0 ? "实时UTC：" : queryTimeSource == 1 ? "自定义UTC：" : "JD(TT)：";
            const std::string activeTimeValue = queryTimeSource == 0 ? formatGregorianTime(utc) : queryTimeSource == 1 ? customUtcText : queryJulianDate;
            const auto timePiece = [&](int segment, int value, int width) {
                std::ostringstream out; out << std::setfill('0') << std::setw(width) << value;
                Element element = text(out.str());
                if (queryTimeEditing && queryTimeSegment == segment) element = element | underlined | bold;
                return element;
            };
            const Element customTimeRow = hbox({text(std::string(queryField == 1 ? "▶ " : "  ") + "自定义UTC："),
                timePiece(0, queryCustomUtc.year, 4), text("-"), timePiece(1, queryCustomUtc.month, 2), text("-"), timePiece(2, queryCustomUtc.day, 2), text(" "),
                timePiece(3, queryCustomUtc.hour, 2), text(":"), timePiece(4, queryCustomUtc.minute, 2), text(":"), timePiece(5, queryCustomUtc.second, 2)});
            const Element dialog = vbox({text("坐标查询") | bold | center, separator(),
                field(0, "时间来源：", sourceLabel), queryTimeSource == 1 ? customTimeRow : field(1, activeTimeLabel, activeTimeValue),
                field(2, "纬度：", queryLatitude + (queryNorth ? " N" : " S")),
                field(3, "经度：", queryLongitude + (queryEast ? " E" : " W")), separator(),
                text(inputStatus),
                separator(),
                text("真太阳时（本地时间）" + formatGregorianTime(queryTime)),
                text(std::string("干支: ") + queryYear.gan + queryYear.zhi + "年 " + queryMonth.gan + queryMonth.zhi + "月 " + queryDay.gan + queryDay.zhi + "日 " + queryHour.hourGan + queryHour.hourZhi + "时"),
                separator(), hbox({
                    vbox({text("太阳") | bold, text("视黄经  " + std::to_string(querySun.apparentEclipticLongitude) + "°"), text("黄纬    " + std::to_string(querySun.eclipticLatitude) + "°"), text("赤纬    " + std::to_string(querySun.declination) + "°"), text("日地距  " + std::to_string(querySun.distanceAU) + " AU")}) | border | flex,
                    vbox({text("太阳活动") | bold, text("天文曙光  " + qevent(queryEvents.astronomicalDawn)), text("航海曙光  " + qevent(queryEvents.nauticalDawn)), text("民用曙光  " + qevent(queryEvents.civilDawn)), text("日出      " + qevent(queryEvents.sunrise)), text("中天      " + qevent(queryEvents.solarNoon)), text("日落      " + qevent(queryEvents.sunset)), text("民用暮光  " + qevent(queryEvents.civilDusk)), text("航海暮光  " + qevent(queryEvents.nauticalDusk)), text("天文暮光  " + qevent(queryEvents.astronomicalDusk)), separator(), text("日照时间  " + qdaylight())}) | border | flex,
                    vbox({text("月球") | bold, text("黄经    " + std::to_string(queryMoon.eclipticLongitude) + "°"), text("黄纬    " + std::to_string(queryMoon.eclipticLatitude) + "°"), text("赤纬    " + std::to_string(queryMoon.declination) + "°"), text("地月距  " + std::to_string(queryMoon.distanceKm) + " km"), text("角距    " + std::to_string(queryMoon.elongation) + "°"), text("照明    " + std::to_string(queryMoon.illuminatedFraction * 100.0) + "%")}) | border | flex}),
                separator(), text(queryTimeEditing
                    ? "[←→] 选择年月日时分秒  [↑↓] 调整（自动校准日期）  [Enter] 完成"
                    : "[↑↓] 选择  [Tab] 切换该行模式  [Enter] 选择  [F/Esc] 关闭") | dim | center}) | border | size(WIDTH, LESS_THAN, 120) | center;
            return dbox({base, dialog | clear_under});
        }
        if (!showGlossary) return base;

        const Element glossary = vbox({
            text("术语说明 / Glossary") | bold | center,
            separator(),
            text("astrodar 使用本地解析公式计算太阳、月球与历法数据。"),
            text("Astronomical positions are calculated locally from analytical series."),
            separator(),
            text("UTC  — 协调世界时 / Coordinated Universal Time"),
            text("TT   — 地球时；用于天文公式 / Terrestrial Time"),
            text("JD   — 儒略日 / Julian Date"),
            text("MJD  — 简化儒略日 / Modified Julian Date"),
            text("黄经 — 黄道坐标中的经度 / Ecliptic Longitude"),
            text("赤纬 — 赤道坐标中的纬度 / Declination"),
            text("节气 — 太阳视黄经每 15° 的瞬间 / Solar Term"),
            text("干支 — 天干地支循环纪法 / Sexagenary Cycle"),
            text("月相 — 日月黄经差决定的受光形状 / Lunar Phase"),
            separator(),
            text("[? / Esc] 关闭") | dim | center,
        }) | border | size(WIDTH, LESS_THAN, 72) | center;
        return dbox({base, glossary | clear_under});
    });
    auto app = CatchEvent(renderer, [&](Event event) {
        if (showEditLocation) {
            if (confirmDeleteLocation) {
                if (event == Event::Escape) {
                    confirmDeleteLocation = false;
                    return true;
                }
                if (event == Event::Return && !panels.empty()) {
                    const std::vector<LocationPanel> previous = panels;
                    panels.erase(panels.begin() + editLocationIndex);
                    if (!saveLocationPanels(panels)) {
                        panels = previous;
                        editLocationStatus = "无法写入地点配置文件，删除未生效。";
                    } else {
                        if (!panels.empty()) editLocationIndex = std::min(editLocationIndex, static_cast<int>(panels.size()) - 1);
                        else editLocationIndex = 0;
                        editLocationSelected = false;
                        editLocationStatus = "地点已删除。";
                    }
                    confirmDeleteLocation = false;
                    return true;
                }
                return true;
            }
            if (event == Event::Escape || event == Event::Character('e') || event == Event::Character('E')) {
                if (editLocationSelected) editLocationSelected = false;
                else showEditLocation = false;
                return true;
            }
            if (panels.empty()) return true;
            if (!editLocationSelected) {
                if (event == Event::ArrowUp) {
                    editLocationIndex = (editLocationIndex + static_cast<int>(panels.size()) - 1) % static_cast<int>(panels.size());
                    return true;
                }
                if (event == Event::ArrowDown) {
                    editLocationIndex = (editLocationIndex + 1) % static_cast<int>(panels.size());
                    return true;
                }
                if (event == Event::Return) {
                    editLocationSelected = true;
                    editLocationStatus.clear();
                    return true;
                }
                return true;
            }
            if (event == Event::Return) {
                editLocationSelected = false;
                return true;
            }
            if (event == Event::Backspace) {
                confirmDeleteLocation = true;
                return true;
            }
            const int direction = event == Event::ArrowUp ? -1 : event == Event::ArrowDown ? 1 : 0;
            if (direction != 0) {
                const int destination = editLocationIndex + direction;
                if (destination < 0 || destination >= static_cast<int>(panels.size())) return true;
                std::swap(panels[editLocationIndex], panels[destination]);
                if (!saveLocationPanels(panels)) {
                    std::swap(panels[editLocationIndex], panels[destination]);
                    editLocationStatus = "无法写入地点配置文件，排序未生效。";
                } else {
                    editLocationIndex = destination;
                    editLocationStatus.clear();
                }
            }
            return true;
        }
        if (page == 0 && !showAddLocation && !showQuery && !showGlossary && (event == Event::Character('a') || event == Event::Character('A'))) {
            showAddLocation = true;
            return true;
        }
        if (page == 0 && !showAddLocation && !showQuery && !showGlossary && (event == Event::Character('e') || event == Event::Character('E'))) {
            showEditLocation = true;
            editLocationIndex = 0;
            editLocationSelected = false;
            confirmDeleteLocation = false;
            editLocationStatus.clear();
            return true;
        }
        if (showAddLocation) {
            if (addTimeZoneSelecting) {
                const std::vector<std::string> matches = filterTimeZones(ianaTimeZones, addTimeZoneFilter);
                if (event == Event::Escape) { addTimeZoneSelecting = false; return true; }
                if (event == Event::ArrowUp && !matches.empty()) {
                    addTimeZoneSelection = (addTimeZoneSelection + static_cast<int>(matches.size()) - 1) % static_cast<int>(matches.size());
                    return true;
                }
                if (event == Event::ArrowDown && !matches.empty()) {
                    addTimeZoneSelection = (addTimeZoneSelection + 1) % static_cast<int>(matches.size());
                    return true;
                }
                if (event == Event::Return && !matches.empty()) {
                    addTimeZone = matches[addTimeZoneSelection];
                    addTimeZoneSelecting = false;
                    return true;
                }
                if (event == Event::Backspace) {
                    if (!addTimeZoneFilter.empty()) addTimeZoneFilter.pop_back();
                    addTimeZoneSelection = 0;
                    return true;
                }
                const std::string character = event.is_character() ? event.character() : "";
                const bool filterCharacter = character == "/" || character == "_" || character == "-" ||
                    (character.size() == 1 && std::isalpha(static_cast<unsigned char>(character[0])));
                if (filterCharacter) {
                    addTimeZoneFilter += character;
                    addTimeZoneSelection = 0;
                    return true;
                }
                return true;
            }
            if (event == Event::Escape) { showAddLocation = false; return true; }
            const int confirmField = addLocationKind == 0 ? 2 : 4;
            const int fieldCount = confirmField + 1;
            if (event == Event::ArrowUp) { addLocationField = (addLocationField + fieldCount - 1) % fieldCount; return true; }
            if (event == Event::ArrowDown) { addLocationField = (addLocationField + 1) % fieldCount; return true; }
            if (event == Event::Tab && addLocationField == 0) {
                addLocationKind = 1 - addLocationKind;
                addLocationField = 0;
                return true;
            }
            if (event == Event::Tab && addLocationKind == 1 && addLocationField == 2) { addNorth = !addNorth; return true; }
            if (event == Event::Tab && addLocationKind == 1 && addLocationField == 3) { addEast = !addEast; return true; }
            if (event == Event::Return && addLocationKind == 0 && addLocationField == 1) {
                addTimeZoneFilter.clear();
                const auto current = std::find(ianaTimeZones.begin(), ianaTimeZones.end(), addTimeZone);
                addTimeZoneSelection = current == ianaTimeZones.end() ? 0 : static_cast<int>(current - ianaTimeZones.begin());
                addTimeZoneSelecting = true;
                return true;
            }
            if (event == Event::Return && addLocationField == confirmField) {
                if (panels.size() >= 4) {
                    addLocationStatus = "最多只能显示四个地点，请先删除已有地点。";
                    return true;
                }
                LocationPanel panel;
                if (addLocationKind == 0) {
                    const auto reference = findTimeZoneReference(addTimeZone);
                    if (!reference) {
                        addLocationStatus = "未找到该 IANA 时区的参考坐标。";
                        return true;
                    }
                    panel = {LocationPanelKind::TimeZone, addTimeZone, addTimeZone,
                             reference->latitudeDegrees, reference->longitudeDegrees};
                } else {
                    const auto latitude = parseCoordinate(addLatitude, 90.0);
                    const auto longitude = parseCoordinate(addLongitude, 180.0);
                    if (addCoordinateName.empty()) {
                        addLocationStatus = "地点名称不能为空。";
                        return true;
                    }
                    if (!latitude) {
                        addLocationStatus = "纬度格式或范围错误（应为 0–90°）。";
                        return true;
                    }
                    if (!longitude) {
                        addLocationStatus = "经度格式或范围错误（应为 0–180°）。";
                        return true;
                    }
                    panel = {LocationPanelKind::Coordinates, addCoordinateName, "",
                             addNorth ? *latitude : -*latitude, addEast ? *longitude : -*longitude};
                }
                panels.push_back(panel);
                if (!saveLocationPanels(panels)) {
                    panels.pop_back();
                    addLocationStatus = "无法写入地点配置文件。";
                    return true;
                }
                showAddLocation = false;
                addLocationStatus.clear();
                return true;
            }
            std::string* value = addLocationKind == 1 && addLocationField == 1 ? &addCoordinateName
                : addLocationKind == 1 && addLocationField == 2 ? &addLatitude
                : addLocationKind == 1 && addLocationField == 3 ? &addLongitude : nullptr;
            if (value && event == Event::Backspace) { if (!value->empty()) value->pop_back(); return true; }
            const std::string character = event.is_character() ? event.character() : "";
            const bool coordinateCharacter = character == "°" || character == "′" || character == "″" || character == "'" ||
                character == "\"" || character == " " || character == "." || character == "d" || character == "m" || character == "s" ||
                character == "-" || (character.size() == 1 && std::isdigit(static_cast<unsigned char>(character[0])));
            const bool timeZoneCharacter = character == "/" || character == "_" || character == "-" ||
                (character.size() == 1 && std::isalpha(static_cast<unsigned char>(character[0])));
            const bool nameCharacter = !character.empty() && character != "\"" && character != "\\";
            if (value && ((addLocationKind == 0 && timeZoneCharacter) ||
                          (addLocationKind == 1 && addLocationField == 1 && nameCharacter) ||
                          (addLocationKind == 1 && addLocationField != 1 && coordinateCharacter))) {
                *value += character;
                return true;
            }
            return true;
        }
        if (event == Event::Character('f') || event == Event::Character('F')) { showQuery = !showQuery; return true; }
        if (showQuery) {
            if (event == Event::Escape) { showQuery = false; return true; }
            if (queryTimeEditing) {
                if (event == Event::Return) { queryTimeEditing = false; return true; }
                if (event == Event::ArrowLeft) { queryTimeSegment = (queryTimeSegment + 5) % 6; return true; }
                if (event == Event::ArrowRight) { queryTimeSegment = (queryTimeSegment + 1) % 6; return true; }
                if (event == Event::ArrowUp) { adjustEditableUtc(queryCustomUtc, queryTimeSegment, 1); return true; }
                if (event == Event::ArrowDown) { adjustEditableUtc(queryCustomUtc, queryTimeSegment, -1); return true; }
                return true;
            }
            if (event == Event::ArrowUp) {
                // 实时 UTC 没有可编辑值，焦点在此模式下直接跳过时间值行。
                queryField = queryTimeSource == 0
                    ? (queryField == 0 ? 3 : queryField == 2 ? 0 : 2)
                    : (queryField + 3) % 4;
                return true;
            }
            if (event == Event::ArrowDown) {
                queryField = queryTimeSource == 0
                    ? (queryField == 0 ? 2 : queryField == 2 ? 3 : 0)
                    : (queryField + 1) % 4;
                return true;
            }
            if (event == Event::Tab && queryField == 0) {
                queryTimeSource = (queryTimeSource + 1) % 3;
                // 进入自定义 UTC 时从当前 UTC 开始，避免携带上次查询的旧时间。
                if (queryTimeSource == 1) {
                    const GregorianTime current = unixToGregorianUTC(std::time(nullptr));
                    queryCustomUtc = {current.year, current.month, current.day,
                                      current.hour, current.minute, current.second};
                }
                return true;
            }
            if (event == Event::Tab && queryField == 2) { queryNorth = !queryNorth; return true; }
            if (event == Event::Tab && queryField == 3) { queryEast = !queryEast; return true; }
            if (event == Event::Return && queryField == 1 && queryTimeSource == 1) { queryTimeEditing = true; return true; }
            std::string* value = queryField == 1 ? (queryTimeSource == 2 ? &queryJulianDate : nullptr)
                : queryField == 2 ? &queryLatitude : queryField == 3 ? &queryLongitude : nullptr;
            if (value && event == Event::Backspace) { if (!value->empty()) value->pop_back(); return true; }
            const std::string character = event.is_character() ? event.character() : "";
            const bool coordinateCharacter = character == "°" || character == "′" || character == "″" || character == "'" ||
                character == "\"" || character == " " || character == "." || character == "d" || character == "m" || character == "s" ||
                character == "-" || character == ":" || (character.size() == 1 && std::isdigit(static_cast<unsigned char>(character[0])));
            if (value && coordinateCharacter) { *value += character; return true; }
            return true;
        }
        if (event == Event::Character('q') || event == Event::Escape) {
            if (showGlossary) {
                showGlossary = false;
                return true;
            }
            screen.Exit();
            return true;
        }
        if (event == Event::Character('?')) {
            showGlossary = !showGlossary;
            return true;
        }
        if (event == Event::Tab) {
            page = 1 - page;
            return true;
        }
        if (event == Event::Character('+')) {
            const int current = refreshMilliseconds.load();
            refreshMilliseconds.store(current < 10000 ? current + 100 : current);
            return true;
        }
        if (event == Event::Character('-')) {
            const int current = refreshMilliseconds.load();
            refreshMilliseconds.store(current > 100 ? current - 100 : current);
            return true;
        }
        return false;
    });
    std::thread ticker([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(refreshMilliseconds.load()));
            screen.Post(Event::Custom);
        }
    });
    screen.Loop(app);
    running = false;
    ticker.join();
    return 0;
}
