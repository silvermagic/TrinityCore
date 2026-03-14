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
 * @file Weather.cpp
 * @brief 天气系统实现文件 - 包含天气生成和更新的核心逻辑
 *
 * 实现功能:
 * - Weather类的构造与初始化
 * - 天气状态更新循环
 * - 基于季节和概率的天气生成算法
 * - 天气数据包广播
 * - 天气状态转换计算
 *
 * @ingroup world
 */

#include "Weather.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "MiscPackets.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "Util.h"
#include "World.h"

/**
 * @brief 构造函数 - 创建区域天气对象
 * @param map 所属地图实例
 * @param zoneId 区域ID
 * @param weatherChances 天气概率配置数据
 *
 * 初始化流程:
 * 1. 保存地图和区域引用
 * 2. 设置天气变化间隔(从worldserver.conf读取CONFIG_INTERVAL_CHANGEWEATHER)
 * 3. 初始化天气状态为晴天,强度为0
 * 4. 记录启动日志
 */
Weather::Weather(Map* map, uint32 zoneId, WeatherData const* weatherChances)
    : m_map(map), m_zone(zoneId), m_weatherChances(weatherChances)
{
    // 设置天气变化间隔(分钟转换为毫秒)
    m_timer.SetInterval(sWorld->getIntConfig(CONFIG_INTERVAL_CHANGEWEATHER));
    // 初始状态为晴天
    m_type = WEATHER_TYPE_FINE;
    m_intensity = 0;

    TC_LOG_INFO("misc", "WORLD: Starting weather system for zone {} (change every {} minutes).", m_zone, (uint32)(m_timer.GetInterval() / (MINUTE*IN_MILLISECONDS)));
}

/**
 * @brief 更新天气系统 - 每个地图tick调用一次
 * @param diff 距离上次更新的时间间隔(毫秒)
 * @return true继续运行,false删除此天气对象(区域内无玩家)
 *
 * 更新流程:
 * 1. 更新定时器
 * 2. 定时器到期时调用ReGenerate()生成新天气
 * 3. 如果天气变化,调用UpdateWeather()广播给玩家
 * 4. 如果UpdateWeather()返回false(无玩家),返回false让调用者删除此对象
 * 5. 触发脚本回调
 */
bool Weather::Update(uint32 diff)
{
    // 更新定时器,确保定时器值为非负
    if (m_timer.GetCurrent() >= 0)
        m_timer.Update(diff);
    else
        m_timer.SetCurrent(0);

    // 定时器到期,重新生成天气
    if (m_timer.Passed())
    {
        m_timer.Reset();
        // 仅当天气发生变化时才更新
        if (ReGenerate())
        {
            // 更新天气,如果区域内没有玩家则返回false删除此对象
            if (!UpdateWeather())
                return false;
        }
    }

    // 触发脚本OnWeatherUpdate回调
    sScriptMgr->OnWeatherUpdate(this, diff);
    return true;
}

/**
 * @brief 重新生成天气 - 核心天气生成算法
 * @return true表示天气发生变化,false表示天气未变化
 *
 * 天气变化统计算法:
 * - 30%概率 - 天气保持不变
 * - 30%概率 - 天气好转(如果当前不是晴天)或改变天气类型
 * - 30%概率 - 天气恶化(如果当前不是晴天)
 * - 10%概率 - 剧烈变化(如果当前不是晴天)
 *
 * 季节计算:
 * - 春季: 从3月20日开始(一年第78天)
 * - 每季约91天
 * - 使用游戏时间计算当前季节
 *
 * 强度分级:
 * - 轻度: 0.0 ~ 0.3333
 * - 中度: 0.3334 ~ 0.6666
 * - 重度: 0.6667 ~ 1.0
 *
 * 新天气生成时强度概率:
 * - 85%概率轻度
 * - 7%概率中度
 * - 7%概率重度
 */
bool Weather::ReGenerate()
{
    // 如果没有天气配置数据,强制设为晴天
    if (!m_weatherChances)
    {
        m_type = WEATHER_TYPE_FINE;
        m_intensity = 0.0f;
        return false;
    }

    // 天气变化统计:
    // - 30%概率无变化
    // - 30%概率变好或改变类型
    // - 30%概率变差
    // - 10%概率剧烈变化
    uint32 u = urand(0, 99);

    // 30%概率保持不变
    if (u < 30)
        return false;

    // 记录旧值用于判断是否变化
    WeatherType old_type = m_type;
    float old_intensity = m_intensity;

    // 计算当前季节
    // 78天 = 1月1日到3月20日的天数(春季开始)
    // 365/4 ≈ 91天为一个季节
    // 季节来源: http://aa.usno.navy.mil/data/docs/EarthSeasons.html
    time_t gtime = GameTime::GetGameTime();
    struct tm ltime;
    localtime_r(&gtime, &ltime);
    uint32 season = ((ltime.tm_yday - 78 + 365)/91)%4;

    static char const* seasonName[WEATHER_SEASONS] = { "spring", "summer", "fall", "winter" };

    TC_LOG_INFO("misc", "Generating a change in {} weather for zone {}.", seasonName[season], m_zone);

    // 如果当前强度很低(< 1/3),30%-60%概率转为晴天
    if ((u < 60) && (m_intensity < 0.33333334f))
    {
        m_type = WEATHER_TYPE_FINE;
        m_intensity = 0.0f;
    }

    // 如果当前不是晴天,30%-60%概率强度降低1/3
    if ((u < 60) && (m_type != WEATHER_TYPE_FINE))
    {
        m_intensity -= 0.33333334f;
        return true;
    }

    // 如果当前不是晴天,60%-90%概率强度增加1/3
    if ((u < 90) && (m_type != WEATHER_TYPE_FINE))
    {
        m_intensity += 0.33333334f;
        return true;
    }

    // 90%-100%概率进行剧烈变化(如果当前不是晴天)
    if (m_type != WEATHER_TYPE_FINE)
    {
        /// 剧烈变化逻辑:
        /// - 如果是轻度 -> 变成重度
        /// - 如果是中度 -> 改变天气类型(先设为晴天)
        /// - 如果是重度 -> 50%变轻度,50%改变天气类型

        if (m_intensity < 0.33333334f)
        {
            // 轻度直接变重度
            m_intensity = 0.9999f;
            return true;
        }
        else
        {
            if (m_intensity > 0.6666667f)
            {
                // 重度有50%概率变轻度
                uint32 rnd = urand(0, 99);
                if (rnd < 50)
                {
                    m_intensity -= 0.6666667f;
                    return true;
                }
            }
            // 中度或重度50%概率转为晴天
            m_type = WEATHER_TYPE_FINE;
            m_intensity = 0;
        }
    }

    // 到这里,说明当前是晴天或需要生成新的天气类型
    // 根据季节配置的概率决定天气类型
    uint32 chance1 = m_weatherChances->data[season].rainChance;
    uint32 chance2 = chance1+ m_weatherChances->data[season].snowChance;
    uint32 chance3 = chance2+ m_weatherChances->data[season].stormChance;

    uint32 rnd = urand(1, 100);
    if (rnd <= chance1)
        m_type = WEATHER_TYPE_RAIN;       // 下雨
    else if (rnd <= chance2)
        m_type = WEATHER_TYPE_SNOW;       // 下雪
    else if (rnd <= chance3)
        m_type = WEATHER_TYPE_STORM;      // 风暴/沙尘暴
    else
        m_type = WEATHER_TYPE_FINE;       // 晴天

    /// 新天气的强度概率分布:
    /// - 85%概率轻度 (0.0 ~ 0.3333)
    /// - 7%概率中度 (0.3334 ~ 0.6666)
    /// - 7%概率重度 (0.6667 ~ 1.0)
    /// 如果是晴天,强度固定为0

    if (m_type == WEATHER_TYPE_FINE)
    {
        m_intensity = 0.0f;
    }
    else if (u < 90)
    {
        // 普通变化,85%概率轻度强度
        m_intensity = (float)rand_norm() * 0.3333f;
    }
    else
    {
        // 剧烈变化,中度和重度各50%概率
        rnd = urand(0, 99);
        if (rnd < 50)
            m_intensity = (float)rand_norm() * 0.3333f + 0.3334f;  // 中度
        else
            m_intensity = (float)rand_norm() * 0.3333f + 0.6667f;  // 重度
    }

    // 只有当天气类型或强度发生变化时才返回true
    return m_type != old_type || m_intensity != old_intensity;
}

/**
 * @brief 向指定玩家发送当前天气更新包
 * @param player 目标玩家指针
 *
 * 构造天气数据包并发送给指定玩家
 */
void Weather::SendWeatherUpdateToPlayer(Player* player)
{
    WorldPackets::Misc::Weather weather(GetWeatherState(), m_intensity);
    player->SendDirectMessage(weather.Write());
}

/**
 * @brief 向指定玩家发送晴天天气包(静态方法)
 * @param player 目标玩家指针
 *
 * 强制发送晴天状态给玩家,用于没有天气配置的区域
 */
void Weather::SendFineWeatherUpdateToPlayer(Player* player)
{
    WorldPackets::Misc::Weather weather(WEATHER_STATE_FINE);
    player->SendDirectMessage(weather.Write());
}

/**
 * @brief 更新并广播天气到区域内所有玩家
 * @return true表示成功发送,false表示区域内没有玩家
 *
 * 执行流程:
 * 1. 限制强度在有效范围内(0.0001 ~ 0.9999)
 * 2. 获取对应的天气状态枚举
 * 3. 构造天气数据包
 * 4. 向区域内所有玩家广播
 * 5. 记录日志并触发脚本回调
 *
 * @note 如果区域内没有玩家,返回false告知调用者删除此Weather对象
 */
bool Weather::UpdateWeather()
{
    // 限制强度在有效范围内,避免客户端显示异常
    if (m_intensity >= 1)
        m_intensity = 0.9999f;
    else if (m_intensity < 0)
        m_intensity = 0.0001f;

    // 获取天气状态枚举(根据类型和强度计算)
    WeatherState state = GetWeatherState();

    // 构造天气数据包
    WorldPackets::Misc::Weather weather(state, m_intensity);

    // 向区域内所有玩家广播天气包,如果没有玩家则返回false
    if (!m_map->SendZoneMessage(m_zone, weather.Write()))
        return false;

    // 记录天气变化的日志
    char const* wthstr;
    switch (state)
    {
        case WEATHER_STATE_FOG:
            wthstr = "fog";
            break;
        case WEATHER_STATE_LIGHT_RAIN:
            wthstr = "light rain";
            break;
        case WEATHER_STATE_MEDIUM_RAIN:
            wthstr = "medium rain";
            break;
        case WEATHER_STATE_HEAVY_RAIN:
            wthstr = "heavy rain";
            break;
        case WEATHER_STATE_LIGHT_SNOW:
            wthstr = "light snow";
            break;
        case WEATHER_STATE_MEDIUM_SNOW:
            wthstr = "medium snow";
            break;
        case WEATHER_STATE_HEAVY_SNOW:
            wthstr = "heavy snow";
            break;
        case WEATHER_STATE_LIGHT_SANDSTORM:
            wthstr = "light sandstorm";
            break;
        case WEATHER_STATE_MEDIUM_SANDSTORM:
            wthstr = "medium sandstorm";
            break;
        case WEATHER_STATE_HEAVY_SANDSTORM:
            wthstr = "heavy sandstorm";
            break;
        case WEATHER_STATE_THUNDERS:
            wthstr = "thunders";
            break;
        case WEATHER_STATE_BLACKRAIN:
            wthstr = "blackrain";
            break;
        case WEATHER_STATE_FINE:
        default:
            wthstr = "fine";
            break;
    }

    TC_LOG_INFO("misc", "Change the weather of zone {} to {}.", m_zone, wthstr);

    // 触发脚本OnWeatherChange回调
    sScriptMgr->OnWeatherChange(this, state, m_intensity);
    return true;
}

/**
 * @brief 强制设置天气 - 跳过随机生成直接设置天气
 * @param type 天气类型
 * @param intensity 天气强度 (0.0 ~ 1.0)
 *
 * 如果新天气与当前天气相同,则不做任何操作
 * 通常由GM命令或脚本调用
 */
void Weather::SetWeather(WeatherType type, float intensity)
{
    // 如果天气未变化,直接返回
    if (m_type == type && m_intensity == intensity)
        return;

    m_type = type;
    m_intensity = intensity;
    UpdateWeather();
}

/**
 * @brief 获取当前天气状态枚举 - 根据类型和强度计算客户端显示状态
 * @return 天气状态枚举值
 *
 * 强度阈值:
 * - < 0.27: 晴天(无论什么类型)
 * - < 0.40: 轻度效果
 * - < 0.70: 中度效果
 * - >= 0.70: 重度效果
 *
 * 天气类型映射:
 * - RAIN -> 轻雨/中雨/大雨
 * - SNOW -> 轻雪/中雪/大雪
 * - STORM -> 轻沙尘暴/中沙尘暴/重沙尘暴
 * - BLACKRAIN -> 黑雨
 * - THUNDERS -> 雷暴
 * - FINE -> 晴天
 */
WeatherState Weather::GetWeatherState() const
{
    // 强度太低时显示为晴天
    if (m_intensity < 0.27f)
        return WEATHER_STATE_FINE;

    switch (m_type)
    {
        case WEATHER_TYPE_RAIN:
            // 雨天: 根据强度分为轻/中/大雨
            if (m_intensity < 0.40f)
                return WEATHER_STATE_LIGHT_RAIN;
            else if (m_intensity < 0.70f)
                return WEATHER_STATE_MEDIUM_RAIN;
            else
                return WEATHER_STATE_HEAVY_RAIN;
        case WEATHER_TYPE_SNOW:
            // 雪天: 根据强度分为轻/中/大雪
            if (m_intensity < 0.40f)
                return WEATHER_STATE_LIGHT_SNOW;
            else if (m_intensity < 0.70f)
                return WEATHER_STATE_MEDIUM_SNOW;
            else
                return WEATHER_STATE_HEAVY_SNOW;
        case WEATHER_TYPE_STORM:
            // 风暴/沙尘暴: 根据强度分为轻/中/重
            if (m_intensity < 0.40f)
                return WEATHER_STATE_LIGHT_SANDSTORM;
            else if (m_intensity < 0.70f)
                return WEATHER_STATE_MEDIUM_SANDSTORM;
            else
                return WEATHER_STATE_HEAVY_SANDSTORM;
        case WEATHER_TYPE_BLACKRAIN:
            // 黑雨: 特殊天气效果
            return WEATHER_STATE_BLACKRAIN;
        case WEATHER_TYPE_THUNDERS:
            // 雷暴: 特殊天气效果
            return WEATHER_STATE_THUNDERS;
        case WEATHER_TYPE_FINE:
        default:
            return WEATHER_STATE_FINE;
    }
}
