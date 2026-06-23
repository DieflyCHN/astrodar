#pragma once

#include <optional>
#include <string>
#include <vector>

struct TimeZoneReference {
    double latitudeDegrees;
    double longitudeDegrees;
};

// 从操作系统安装的 IANA zone1970.tab / zone.tab 读取时区代表性坐标。
std::optional<TimeZoneReference> findTimeZoneReference(const std::string& timeZone);
std::vector<std::string> listIanaTimeZones();
