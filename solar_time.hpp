#pragma once

#include <ctime>

struct LocalSolarTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
};

// 真太阳时（视地方时）：UTC + 经度修正 + 均时差。
// longitudeDegrees 为东经正、西经负；equationOfTimeMinutes 为太阳模型输出。
LocalSolarTime calculateLocalSolarTime(std::time_t utc, double longitudeDegrees,
                                       double equationOfTimeMinutes);
