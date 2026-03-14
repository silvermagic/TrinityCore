/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file WeatherMgr.cpp
 * @brief 天气管理器实现 - 天气数据加载和查询的具体实现
 *
 * 实现功能:
 * - 从数据库加载天气配置数据
 * - 缓存天气数据到内存哈希表
 * - 提供区域天气数据查询接口
 *
 * 数据库表结构(game_weather):
 * - zone: 区域ID
 * - spring/summer/fall/winter_rain_chance: 四季下雨概率
 * - spring/summer/fall/winter_snow_chance: 四季下雪概率
 * - spring/summer/fall/winter_storm_chance: 四季风暴概率
 * - ScriptName: 脚本名称
 *
 * @ingroup world
 */

#include "WeatherMgr.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Timer.h"
#include "Weather.h"
#include "MiscPackets.h"

namespace WeatherMgr
{

namespace
{
    /**
     * @brief 天气数据缓存哈希表
     *
     * 键: 区域ID (zone_id)
     * 值: WeatherData结构体,包含四季天气概率和脚本ID
     *
     * 在LoadWeatherData()时填充,之后只读访问,线程安全
     */
    std::unordered_map<uint32, WeatherData> _weatherData;
}

/**
 * @brief 获取天气数据 - 从缓存中查询指定区域的天气配置
 * @param zone_id 区域ID
 * @return 天气数据指针,如果区域没有配置则返回nullptr
 *
 * 使用MapGetValuePtr辅助函数安全地从哈希表获取值指针,
 * 避免创建临时对象
 */
WeatherData const* GetWeatherData(uint32 zone_id)
{
    return Trinity::Containers::MapGetValuePtr(_weatherData, zone_id);
}

/**
 * @brief 加载天气数据 - 从数据库加载所有区域的天气配置
 *
 * 执行流程:
 * 1. 查询game_weather表获取所有天气配置
 * 2. 遍历结果集,为每个区域创建WeatherData对象
 * 3. 读取四个季节的天气概率配置(雨/雪/风暴)
 * 4. 验证概率值范围(0-100),超出范围则修正为默认值25并记录错误
 * 5. 解析脚本名称并转换为脚本ID
 * 6. 记录加载统计信息
 *
 * 数据格式说明:
 * - 字段0: zone (区域ID)
 * - 字段1-3: 春季 (rain/snow/storm chance)
 * - 字段4-6: 夏季 (rain/snow/storm chance)
 * - 字段7-9: 秋季 (rain/snow/storm chance)
 * - 字段10-12: 冬季 (rain/snow/storm chance)
 * - 字段13: ScriptName (脚本名称)
 *
 * 错误处理:
 * - 如果表为空,记录警告并返回
 * - 如果概率值>100,修正为25并记录SQL错误
 *
 * @note 调用时机: 服务器启动时,在World::SetInitialWorldSettings()中调用
 * @note 性能: 一次性加载所有数据,加载时间取决于配置的区域数量
 */
void LoadWeatherData()
{
    uint32 oldMSTime = getMSTime();

    uint32 count = 0;

    // 查询game_weather表获取所有区域的天气配置
    QueryResult result = WorldDatabase.Query("SELECT "
        "zone, spring_rain_chance, spring_snow_chance, spring_storm_chance,"
        "summer_rain_chance, summer_snow_chance, summer_storm_chance,"
        "fall_rain_chance, fall_snow_chance, fall_storm_chance,"
        "winter_rain_chance, winter_snow_chance, winter_storm_chance,"
        "ScriptName FROM game_weather");

    // 如果表为空或查询失败,记录警告并返回
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 weather definitions. DB table `game_weather` is empty.");
        return;
    }

    // 遍历查询结果,加载每个区域的天气配置
    do
    {
        Field* fields = result->Fetch();

        uint32 zone_id = fields[0].GetUInt32();

        // 获取或创建该区域的天气数据对象
        WeatherData& wzc = _weatherData[zone_id];

        // 加载四个季节的天气概率配置
        // 每个季节3个字段: rain/snow/storm chance
        // 字段索引: season * (MAX_WEATHER_TYPE-1) + 偏移量
        for (uint8 season = 0; season < WEATHER_SEASONS; ++season)
        {
            wzc.data[season].rainChance  = fields[season * (MAX_WEATHER_TYPE-1) + 1].GetUInt8();
            wzc.data[season].snowChance  = fields[season * (MAX_WEATHER_TYPE-1) + 2].GetUInt8();
            wzc.data[season].stormChance = fields[season * (MAX_WEATHER_TYPE-1) + 3].GetUInt8();

            // 验证下雨概率范围(0-100)
            if (wzc.data[season].rainChance > 100)
            {
                wzc.data[season].rainChance = 25;  // 修正为默认值
                TC_LOG_ERROR("sql.sql", "Weather for zone {} season {} has wrong rain chance > 100%", zone_id, season);
            }

            // 验证下雪概率范围(0-100)
            if (wzc.data[season].snowChance > 100)
            {
                wzc.data[season].snowChance = 25;  // 修正为默认值
                TC_LOG_ERROR("sql.sql", "Weather for zone {} season {} has wrong snow chance > 100%", zone_id, season);
            }

            // 验证风暴概率范围(0-100)
            if (wzc.data[season].stormChance > 100)
            {
                wzc.data[season].stormChance = 25;  // 修正为默认值
                TC_LOG_ERROR("sql.sql", "Weather for zone {} season {} has wrong storm chance > 100%", zone_id, season);
            }
        }

        // 解析脚本名称并转换为脚本ID
        wzc.ScriptId = sObjectMgr->GetScriptId(fields[13].GetString());

        ++count;
    }
    while (result->NextRow());

    // 记录加载统计信息
    TC_LOG_INFO("server.loading", ">> Loaded {} weather definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

} // namespace WeatherMgr
