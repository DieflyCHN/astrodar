#pragma once

#include "observer.hpp"

#include <ctime>
#include <optional>

struct MoonEvents {
    std::optional<std::time_t> standardRise;
    std::optional<std::time_t> upperTransit;
    std::optional<std::time_t> standardSet;
};

// 年鉴等价的站心月出没：真仰角 h = -(34′平均折射 + 当日视半径)。
// 视差已在站心坐标中应用，因此不再重复减去地心地表视差。
MoonEvents calculateMoonEvents(const ObserverLocation& location, int year, int month, int day);
