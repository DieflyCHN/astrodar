#pragma once

#include "observer.hpp"

#include <ctime>
#include <optional>

struct SolarEvents {
    std::optional<std::time_t> sunrise;
    std::optional<std::time_t> solarNoon;
    std::optional<std::time_t> sunset;
    std::optional<std::time_t> civilDawn;
    std::optional<std::time_t> civilDusk;
    std::optional<std::time_t> nauticalDawn;
    std::optional<std::time_t> nauticalDusk;
    std::optional<std::time_t> astronomicalDawn;
    std::optional<std::time_t> astronomicalDusk;
};

SolarEvents calculateSolarEvents(const ObserverLocation& location, int year, int month, int day);
