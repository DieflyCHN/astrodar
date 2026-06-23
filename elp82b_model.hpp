#pragma once

#include <optional>

// ELP2000-82B 的地心 J2000 平黄道球坐标。输入 TT；TDB−TT 的毫秒级差异
// 对当前显示精度可忽略，后续站心/掩食层会单独补偿。
struct ELP82BPosition {
    double longitudeDegrees;
    double latitudeDegrees;
    double distanceKm;
};

std::optional<ELP82BPosition> calculateELP82BPosition(double jdTT);
