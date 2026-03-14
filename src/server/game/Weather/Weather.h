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
 * @file Weather.h
 * @brief 天气系统模块 - 管理游戏世界中的动态天气效果
 *
 * 本模块负责:
 * - 管理特定区域的天气状态(晴天、雨天、雪天、沙尘暴等)
 * - 根据季节和时间动态生成天气变化
 * - 向区域内玩家发送天气更新包
 * - 维护天气变化的定时器
 *
 * @ingroup world
 */

#ifndef __WEATHER_H
#define __WEATHER_H

#include "Common.h"
#include "SharedDefines.h"
#include "Timer.h"

class Map;
class Player;

/** 季节数量常量 - 春、夏、秋、冬四季 */
#define WEATHER_SEASONS 4

/**
 * @struct WeatherSeasonChances
 * @brief 单个季节的天气出现概率配置
 *
 * 存储某个季节中不同天气类型的发生概率(百分比, 0-100)
 */
struct WeatherSeasonChances
{
    uint32 rainChance;    ///< 下雨概率 (0-100)
    uint32 snowChance;    ///< 下雪概率 (0-100)
    uint32 stormChance;   ///< 风暴/沙尘暴概率 (0-100)
};

/**
 * @struct WeatherData
 * @brief 区域天气数据配置结构
 *
 * 存储某个区域在四个季节中的天气概率配置,
 * 从数据库表 game_weather 加载
 */
struct WeatherData
{
    WeatherSeasonChances data[WEATHER_SEASONS];  ///< 四季天气概率配置数组 [0=春, 1=夏, 2=秋, 3=冬]
    uint32 ScriptId;                              ///< 脚本ID,用于天气相关的脚本事件
};

/**
 * @enum WeatherState
 * @brief 天气状态枚举 - 定义所有可能的天气状态类型
 *
 * 这些状态值对应客户端的天气显示效果,
 * 数值来自客户端协议定义
 */
enum WeatherState : uint32
{
    WEATHER_STATE_FINE              = 0,    ///< 晴朗天气
    WEATHER_STATE_FOG               = 1,    ///< 雾天
    WEATHER_STATE_DRIZZLE           = 2,    ///< 毛毛雨
    WEATHER_STATE_LIGHT_RAIN        = 3,    ///< 小雨
    WEATHER_STATE_MEDIUM_RAIN       = 4,    ///< 中雨
    WEATHER_STATE_HEAVY_RAIN        = 5,    ///< 大雨
    WEATHER_STATE_LIGHT_SNOW        = 6,    ///< 小雪
    WEATHER_STATE_MEDIUM_SNOW       = 7,    ///< 中雪
    WEATHER_STATE_HEAVY_SNOW        = 8,    ///< 大雪
    WEATHER_STATE_LIGHT_SANDSTORM   = 22,   ///< 小沙尘暴
    WEATHER_STATE_MEDIUM_SANDSTORM  = 41,   ///< 中沙尘暴
    WEATHER_STATE_HEAVY_SANDSTORM   = 42,   ///< 大沙尘暴
    WEATHER_STATE_THUNDERS          = 86,   ///< 雷暴
    WEATHER_STATE_BLACKRAIN         = 90,   ///< 黑雨(特殊效果)
    WEATHER_STATE_BLACKSNOW         = 106   ///< 黑雪(特殊效果)
};

/**
 * @class Weather
 * @brief 天气管理类 - 管理单个区域的天气系统
 *
 * 每个游戏区域(Zone)都有一个Weather实例,负责:
 * - 周期性生成新的天气状态
 * - 维护天气类型和强度
 * - 向区域内的玩家发送天气更新
 * - 根据季节和概率配置动态计算天气
 *
 * 天气系统工作流程:
 * 1. 构造时设置天气变化间隔(从配置读取)
 * 2. Update()在每次地图更新时被调用,检查定时器
 * 3. 定时器到期时调用ReGenerate()生成新天气
 * 4. 如果天气变化,调用UpdateWeather()通知所有玩家
 *
 * @note 天气强度范围: 0.0(无) ~ 1.0(最强)
 * @note 当区域内没有玩家时,Weather对象会被销毁
 */
class TC_GAME_API Weather
{
    public:

        /**
         * @brief 构造函数 - 初始化区域天气系统
         * @param map 所属地图实例指针
         * @param zoneId 区域ID
         * @param weatherChances 天气概率配置数据指针(可能为nullptr)
         *
         * 初始化天气定时器,设置初始状态为晴天(WEATHER_TYPE_FINE)
         * 定时器间隔由配置CONFIG_INTERVAL_CHANGEWEATHER决定
         */
        Weather(Map* map, uint32 zoneId, WeatherData const* weatherChances);

        /**
         * @brief 析构函数
         */
        ~Weather() { };

        /**
         * @brief 更新天气系统
         * @param diff 距离上次更新的时间间隔(毫秒)
         * @return 返回true表示继续运行,false表示应该删除此Weather对象(区域内无玩家)
         *
         * 每次地图tick时调用,检查天气定时器是否到期,
         * 到期则重新生成天气并通知玩家
         *
         * @note 调用时机: 每个地图更新周期(Map::Update)
         * @note 性能: 轻量级操作,只是检查定时器
         */
        bool Update(uint32 diff);

        /**
         * @brief 重新生成天气
         * @return 返回true表示天气发生了变化,false表示天气未变化
         *
         * 根据当前季节、概率配置和随机数生成新的天气类型和强度
         * 算法考虑:
         * - 30%概率保持不变
         * - 30%概率变好或改变类型
         * - 30%概率变差
         * - 10%概率剧烈变化
         *
         * @note 如果没有天气配置数据(weatherChances为nullptr),总是返回晴天
         */
        bool ReGenerate();

        /**
         * @brief 更新并广播天气到所有玩家
         * @return 返回true表示成功发送给至少一个玩家,false表示区域内没有玩家
         *
         * 将当前天气状态打包并发送给区域内所有在线玩家,
         * 如果区域内没有玩家则返回false(调用者应删除此Weather对象)
         *
         * @note 调用时机: ReGenerate()返回true后自动调用
         */
        bool UpdateWeather();

        /**
         * @brief 向指定玩家发送天气更新包
         * @param player 目标玩家指针
         *
         * 发送当前天气状态到指定玩家,通常用于玩家进入区域时同步天气
         *
         * @note 调用时机: 玩家进入区域或请求天气同步时
         */
        void SendWeatherUpdateToPlayer(Player* player);

        /**
         * @brief 向指定玩家发送晴天天气包(静态方法)
         * @param player 目标玩家指针
         *
         * 强制发送晴天状态给玩家,用于没有天气配置的区域
         *
         * @note 调用时机: 玩家进入没有天气数据的区域时
         */
        static void SendFineWeatherUpdateToPlayer(Player* player);

        /**
         * @brief 强制设置天气
         * @param type 天气类型
         * @param intensity 天气强度 (0.0 ~ 1.0)
         *
         * 直接设置天气状态,跳过随机生成过程,
         * 通常由GM命令或脚本调用
         */
        void SetWeather(WeatherType type, float intensity);

        /**
         * @brief 获取区域ID
         * @return 区域ID
         */
        uint32 GetZone() const { return m_zone; };

        /**
         * @brief 获取天气脚本ID
         * @return 脚本ID,用于天气相关的脚本事件
         */
        uint32 GetScriptId() const { return m_weatherChances->ScriptId; }

    private:

        /**
         * @brief 获取当前天气状态
         * @return 天气状态枚举值
         *
         * 根据天气类型和强度计算对应的天气状态枚举,
         * 用于客户端显示
         *
         * 强度分级:
         * - < 0.27: 晴天
         * - < 0.40: 轻度
         * - < 0.70: 中度
         * - >= 0.70: 重度
         */
        WeatherState GetWeatherState() const;

        Map* m_map;                          ///< 所属地图实例
        uint32 m_zone;                       ///< 区域ID
        WeatherType m_type;                  ///< 当前天气类型(晴天/雨/雪/风暴等)
        float m_intensity;                   ///< 天气强度 (0.0 ~ 1.0)
        IntervalTimer m_timer;               ///< 天气变化定时器
        WeatherData const* m_weatherChances; ///< 天气概率配置数据(可能为nullptr)
};
#endif
