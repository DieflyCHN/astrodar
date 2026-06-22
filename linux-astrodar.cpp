#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "solar_model.hpp"
#include "solar_events.hpp"
#include "solar_time.hpp"
#include "moon_model.hpp"
#include "localization.hpp"
#include "observer.hpp"
#include "solar_terms.hpp"
#include "time_scales.hpp"

constexpr int REPUBLIC_OFFSET = 841;

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
    int queryField = 0;
    std::string queryLatitude = "52.5200";
    std::string queryLongitude = "13.4050";
    bool queryNorth = true;
    bool queryEast = true;
    std::atomic<bool> running{true};
    std::atomic<int> refreshMilliseconds{1000};

    auto renderer = Renderer([&] {
        const std::time_t nowUTC = std::time(nullptr);
        const GregorianTime utc = unixToGregorianUTC(nowUTC);
        const GregorianTime berlin = unixToGregorianInTimezone(nowUTC, BERLIN.timeZone.c_str());
        const GregorianTime beijing = unixToGregorianInTimezone(nowUTC, BEIJING.timeZone.c_str());
        const double jd = unixToJD(nowUTC);
        const double mjd = jd - 2400000.5;
        const auto jdTT = utcJulianDateToTT(jd);
        if (!jdTT) return text("UTC-to-TT conversion unavailable") | border;

        const SolarPosition sun = calculateSolarPosition(*jdTT);
        const MoonPosition moon = calculateMoonPosition(*jdTT, sun.apparentEclipticLongitude);
        const SolarTermEvent current = currentSolarTerm(*jdTT);
        const SolarTermEvent next = nextSolarTerm(*jdTT);
        const auto city = [&](const ObserverLocation& location, const GregorianTime& time) {
            const Ganzhi day = jdnToDayGanzhi(gregorianToJDN(time));
            const Ganzhi year = ganzhiYearAt(time, sun.apparentEclipticLongitude);
            const Ganzhi month = ganzhiMonthAt(year, sun.apparentEclipticLongitude);
            const ChineseHour hour = dayGanzhiToHourGanzhi(day, time.hour, time.minute);
            return vbox({text(location.id) | bold,
                         text(formatGregorianTime(time) + "  " + hour.hourZhi + hour.chuZheng + hour.ke + "刻"),
                         text(std::string("干支: ") + year.gan + year.zhi + "年 " + month.gan + month.zhi + "月 " +
                              day.gan + day.zhi + "日 " + hour.hourGan + hour.hourZhi + "时")});
        };
        const auto eventTime = [&](const ObserverLocation& location, const std::optional<std::time_t>& event) {
            if (!event) return std::string("--:--");
            const GregorianTime localEvent = unixToGregorianInTimezone(*event, location.timeZone.c_str());
            return formatGregorianTime(localEvent).substr(11);
        };
        const SolarEvents berlinEvents = calculateSolarEvents(BERLIN, berlin.year, berlin.month, berlin.day);
        const SolarEvents beijingEvents = calculateSolarEvents(BEIJING, beijing.year, beijing.month, beijing.day);
        const auto daylightDuration = [](const SolarEvents& events) {
            if (!events.sunrise || !events.sunset) return std::string("--:--");
            const long minutes = static_cast<long>((*events.sunset - *events.sunrise) / 60);
            std::ostringstream output;
            output << std::setfill('0') << std::setw(2) << minutes / 60 << ':' << std::setw(2) << minutes % 60;
            return output.str();
        };
        const auto eventTimeline = [&](const ObserverLocation& location, const SolarEvents& events) {
            return vbox({text(location.id) | bold,
                         text("天文曙光  " + eventTime(location, events.astronomicalDawn)),
                         text("航海曙光  " + eventTime(location, events.nauticalDawn)),
                         text("民用曙光  " + eventTime(location, events.civilDawn)),
                         text("日出      " + eventTime(location, events.sunrise)),
                         text("中天      " + eventTime(location, events.solarNoon)),
                         text("日落      " + eventTime(location, events.sunset)),
                         text("民用暮光  " + eventTime(location, events.civilDusk)),
                         text("航海暮光  " + eventTime(location, events.nauticalDusk)),
                         text("天文暮光  " + eventTime(location, events.astronomicalDusk)),
                         separator(), text(tr("daylight") + "  " + daylightDuration(events))});
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
            content = hbox({vbox({city(BERLIN, berlin), separator(), eventTimeline(BERLIN, berlinEvents)}) | border | flex,
                            vbox({city(BEIJING, beijing), separator(), eventTimeline(BEIJING, beijingEvents)}) | border | flex});
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
        const Element title = hbox({text("astrodar  ·  " + (page == 0 ? tr("overview") : tr("astronomical_data"))) | bold | flex,
                                    text(tr("refresh") + ": " + std::to_string(refreshMilliseconds.load()) + " ms  [-/+]" ) | dim});
        const Element base = vbox({title,
                     separator(), content | flex,
                     separator(), text(tr("help")) | dim}) | border;
        if (showQuery) {
            const auto latitudeValue = parseCoordinate(queryLatitude, 90.0);
            const auto longitudeValue = parseCoordinate(queryLongitude, 180.0);
            const double latitude = (queryNorth ? 1.0 : -1.0) * latitudeValue.value_or(0.0);
            const double longitude = (queryEast ? 1.0 : -1.0) * longitudeValue.value_or(0.0);
            const LocalSolarTime solarTime = calculateLocalSolarTime(nowUTC, longitude, sun.equationOfTimeMinutes);
            const GregorianTime queryTime{solarTime.year, solarTime.month, solarTime.day, solarTime.hour, solarTime.minute, solarTime.second, solarTime.weekday};
            const Ganzhi queryDay = jdnToDayGanzhi(gregorianToJDN(queryTime));
            const Ganzhi queryYear = ganzhiYearAt(queryTime, sun.apparentEclipticLongitude);
            const Ganzhi queryMonth = ganzhiMonthAt(queryYear, sun.apparentEclipticLongitude);
            const ChineseHour queryHour = dayGanzhiToHourGanzhi(queryDay, queryTime.hour, queryTime.minute);
            const ObserverLocation queryLocation{"查询", "UTC", latitude, longitude, 0.0};
            const SolarEvents queryEvents = calculateSolarEvents(queryLocation, queryTime.year, queryTime.month, queryTime.day);
            const auto qevent = [&](const std::optional<std::time_t>& event) {
                if (!event) return std::string("--:--");
                const LocalSolarTime time = calculateLocalSolarTime(*event, longitude, sun.equationOfTimeMinutes);
                std::ostringstream out; out << std::setfill('0') << std::setw(2) << time.hour << ':' << std::setw(2) << time.minute; return out.str();
            };
            const auto field = [&](int index, const std::string& label, const std::string& value) {
                return text(std::string(index == queryField ? "▶ " : "  ") + label + value);
            };
            const Element dialog = vbox({text("坐标查询（真太阳时）") | bold | center, separator(),
                field(0, "纬度：", queryLatitude), field(1, "方向：", queryNorth ? "北" : "南"),
                field(2, "经度：", queryLongitude), field(3, "方向：", queryEast ? "东" : "西"), separator(),
                text(latitudeValue && longitudeValue ? "坐标有效" : "坐标格式错误：使用十进制或 度°分′秒″"),
                text(formatGregorianTime(queryTime)),
                text(std::string("干支: ") + queryYear.gan + queryYear.zhi + "年 " + queryMonth.gan + queryMonth.zhi + "月 " + queryDay.gan + queryDay.zhi + "日 " + queryHour.hourGan + queryHour.hourZhi + "时"),
                separator(), text("天文曙光  " + qevent(queryEvents.astronomicalDawn)), text("航海曙光  " + qevent(queryEvents.nauticalDawn)),
                text("民用曙光  " + qevent(queryEvents.civilDawn)), text("日出      " + qevent(queryEvents.sunrise)),
                text("中天      " + qevent(queryEvents.solarNoon)), text("日落      " + qevent(queryEvents.sunset)),
                text("民用暮光  " + qevent(queryEvents.civilDusk)), text("航海暮光  " + qevent(queryEvents.nauticalDusk)), text("天文暮光  " + qevent(queryEvents.astronomicalDusk)),
                separator(), text("[Tab] 字段  [↑↓] 方向  [F/Esc] 关闭") | dim | center}) | border | size(WIDTH, LESS_THAN, 58) | center;
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
        if (event == Event::Character('f') || event == Event::Character('F')) { showQuery = !showQuery; return true; }
        if (showQuery) {
            if (event == Event::Escape) { showQuery = false; return true; }
            if (event == Event::Tab) { queryField = (queryField + 1) % 4; return true; }
            if ((event == Event::ArrowUp || event == Event::ArrowDown) && queryField == 1) { queryNorth = !queryNorth; return true; }
            if ((event == Event::ArrowUp || event == Event::ArrowDown) && queryField == 3) { queryEast = !queryEast; return true; }
            std::string* value = queryField == 0 ? &queryLatitude : queryField == 2 ? &queryLongitude : nullptr;
            if (value && event == Event::Backspace) { if (!value->empty()) value->pop_back(); return true; }
            const std::string character = event.is_character() ? event.character() : "";
            const bool coordinateCharacter = character == "°" || character == "′" || character == "″" || character == "'" ||
                character == "\"" || character == " " || character == "." || character == "d" || character == "m" || character == "s" ||
                (character.size() == 1 && std::isdigit(static_cast<unsigned char>(character[0])));
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
