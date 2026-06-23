#pragma once

#include <string>

// 面板的时间语义必须明确：IANA 时区是民用时间和参考地点，坐标则是
// 不绑定法定时区的实际观测点，统一使用真太阳时。
enum class LocationPanelKind {
    TimeZone,
    Coordinates,
};

struct LocationPanel {
    LocationPanelKind kind;
    std::string id;
    std::string timeZone;  // 仅 TimeZone；配置只保存 IANA 标识符。
    double latitudeDegrees = 0.0;
    double longitudeDegrees = 0.0;
};

// 地点坐标是所有本地天文事件（出没、方位、视差）的唯一地点输入。
// 经度以东为正，纬度以北为正，海拔单位为米。
struct ObserverLocation {
    std::string id;
    std::string timeZone;
    double latitudeDegrees;
    double longitudeDegrees;
    double elevationMeters;
};
