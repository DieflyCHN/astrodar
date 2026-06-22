#include "localization.hpp"

#include <utility>

namespace localization {
std::string_view text(std::string_view key) {
    static constexpr std::pair<std::string_view, std::string_view> entries[] = {
        std::pair{"overview", "概览"}, {"astronomical_data", "天文数据"},
        {"local_time_hint", "地点时间与干支均按各自本地时区显示。"},
        {"lunar_phase", "月相"}, {"illumination", "照明"},
        {"utc", "协调世界时"}, {"unix_time", "Unix 时间戳"},
        {"jd", "儒略日"}, {"mjd", "简化儒略日"}, {"jd_tt", "地球时儒略日"},
        {"sun_longitude", "太阳视黄经"}, {"sun_latitude", "太阳黄纬"},
        {"sun_declination", "太阳赤纬"}, {"sun_distance", "日地距离"},
        {"sun", "太阳"}, {"moon", "月球"},
        {"moon_longitude", "月球黄经"}, {"moon_latitude", "月球黄纬"},
        {"moon_distance", "地月距离"}, {"moon_declination", "月球赤纬"},
        {"moon_elongation", "月相角距"}, {"moon_illumination", "月面照明"},
        {"current_term", "当前节气"}, {"next_term", "下一节气"},
        {"major_terms", "本年主节气"}, {"daylight", "日照时间"}, {"refresh", "刷新"},
        {"help", "[Tab] 页面   [F] 坐标查询   [-/+] 刷新率   [?] 术语说明   [q/Esc] 退出"},
    };
    for (const auto& entry : entries) if (entry.first == key) return entry.second;
    return key;
}
}
