#pragma once

#include <string>

// 地点坐标是所有本地天文事件（出没、方位、视差）的唯一地点输入。
// 经度以东为正，纬度以北为正，海拔单位为米。
struct ObserverLocation {
    std::string id;
    std::string timeZone;
    double latitudeDegrees;
    double longitudeDegrees;
    double elevationMeters;
};
