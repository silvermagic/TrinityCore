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
 * @file WeatherMgr.h
 * @brief 天气管理器模块 - 负责加载和查询天气配置数据
 *
 * 本模块负责:
 * - 从数据库加载天气配置(game_weather表)
 * - 提供按区域ID查询天气数据的接口
 * - 管理天气配置数据的缓存
 *
 * 使用方式:
 * - 服务器启动时调用LoadWeatherData()加载所有天气配置
 * - 通过GetWeatherData()查询特定区域的天气配置
 *
 * @ingroup world
 */

#ifndef __WEATHERMGR_H
#define __WEATHERMGR_H

#include "Define.h"

class Weather;
class Player;
struct WeatherData;

/**
 * @namespace WeatherMgr
 * @brief 天气管理命名空间 - 提供天气数据加载和查询功能
 *
 * 此命名空间封装了天气系统的全局管理功能,
 * 作为天气数据的统一入口点
 */
namespace WeatherMgr
{
    /**
     * @brief 加载天气数据 - 从数据库加载所有区域的天气配置
     *
     * 从game_weather表读取每个区域四季的天气概率配置,
     * 并缓存到内存中供后续查询
     *
     * @note 调用时机: 服务器启动时,在World::SetInitialWorldSettings()中调用
     * @note 性能: 一次性加载所有数据到内存,后续查询为O(1)复杂度
     */
    TC_GAME_API void LoadWeatherData();

    /**
     * @brief 获取天气数据 - 查询指定区域的天气配置
     * @param zone_id 区域ID
     * @return 天气数据指针,如果区域没有配置则返回nullptr
     *
     * 返回的WeatherData包含四个季节的天气概率配置和脚本ID
     *
     * @note 调用时机: 创建Weather对象时(Map::GetWeather())
     * @note 性能: O(1)哈希查找,非常高效
     * @note 线程安全: 数据加载后只读,无需加锁
     */
    TC_GAME_API WeatherData const* GetWeatherData(uint32 zone_id);
}

#endif
