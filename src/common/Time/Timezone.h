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

#ifndef TRINITYCORE_TIMEZONE_H
#define TRINITYCORE_TIMEZONE_H

#include "Define.h"
#include "Duration.h"
#include <string>

/**
 * @file Timezone.h
 * @brief 时区处理模块
 *
 * @details 该模块提供了跨平台的时区信息查询和转换功能,主要用于:
 *   - 解析客户端认证包中的时区偏移哈希值
 *   - 获取系统当前时区信息
 *   - 查找与客户端兼容的时区设置
 *
 * 时区处理原理:
 *   1. 时区偏移以分钟为单位表示相对于UTC的偏移量
 *   2. 客户端使用FNV-1a哈希算法对时区偏移字符串进行哈希
 *   3. 服务器通过哈希值反向查找对应的时区偏移量
 *   4. 支持夏令时(DST)调整,会影响实际偏移量
 *
 * 平台差异:
 *   - Windows: 使用C++20 <chrono>时区库
 *   - Linux/macOS: 使用POSIX localtime和boost::locale
 *
 * 使用场景:
 *   - 客户端认证: 解析客户端发送的时区哈希
 *   - 时间显示: 将服务器时间转换为本地时间
 *   - 时区匹配: 查找客户端支持的时区
 */

namespace Trinity::Timezone
{
/**
 * @brief 根据哈希值获取时区偏移量
 *
 * @details 客户端在认证包中发送时区偏移量的哈希值,此函数用于反查对应的偏移量。
 *          哈希值由客户端对时区偏移字符串(分钟数)进行FNV-1a哈希计算得出。
 *
 * @param hash 时区偏移量字符串的FNV-1a哈希值
 *
 * @return Minutes 时区偏移量(分钟),如果哈希值未找到则返回0(UTC时区)
 *
 * @note 返回值范围通常在 -720(UTC-12) 到 840(UTC+14) 分钟之间
 *
 * @see InitTimezoneHashDb() 了解哈希表的构建过程
 */
TC_COMMON_API Minutes GetOffsetByHash(uint32 hash);

/**
 * @brief 获取系统时区在指定时间点的偏移量
 *
 * @details 计算系统当前时区在指定时间点相对于UTC的偏移量,考虑夏令时影响。
 *          该偏移量必须加到UTC时间上才能得到本地时间。
 *
 * @param date 要查询的系统时间点
 *
 * @return Minutes 时区偏移量(分钟),包含夏令时调整(如适用)
 *
 * @note Windows平台使用C++20 chrono时区库,其他平台使用POSIX tm_gmtoff字段
 *
 * @code
 * auto now = std::chrono::system_clock::now();
 * Minutes offset = GetSystemZoneOffsetAt(now);
 * // offset = UTC时间转换为本地时间需要增加的分钟数
 * @endcode
 */
TC_COMMON_API Minutes GetSystemZoneOffsetAt(SystemTimePoint date);

/**
 * @brief 获取系统时区偏移量
 *
 * @details 获取系统当前时区相对于UTC的偏移量,可选择是否考虑夏令时。
 *          这是获取系统时区偏移的便捷方法。
 *
 * @param applyDst 是否应用夏令时调整(默认为true)
 *                 - true: 使用当前时间,包含夏令时调整
 *                 - false: 使用Unix纪元时间(1970-01-01),不包含夏令时
 *
 * @return Minutes 时区偏移量(分钟)
 *
 * @note 夏令时通常会额外增加60分钟偏移量
 *
 * @code
 * // 获取包含夏令时的当前偏移
 * Minutes currentOffset = GetSystemZoneOffset(true);
 *
 * // 获取标准时区偏移(不含夏令时)
 * Minutes standardOffset = GetSystemZoneOffset(false);
 * @endcode
 */
TC_COMMON_API Minutes GetSystemZoneOffset(bool applyDst = true);

/**
 * @brief 获取系统时区名称
 *
 * @details 返回系统当前时区的IANA标准名称字符串,如"Asia/Shanghai"、"America/New_York"等。
 *          IANA时区数据库包含全球各地区的时区定义和历史变更记录。
 *
 * @return std::string IANA时区标识符
 *
 * @note 返回值示例:
 *   - 中国大陆: "Asia/Shanghai"
 *   - 美国东部: "America/New_York"
 *   - 欧洲: "Europe/Paris"
 *   - UTC: "Etc/UTC"
 *
 * @see FindClosestClientSupportedTimezone() 用于查找客户端兼容的时区
 */
TC_COMMON_API std::string GetSystemZoneName();

/**
 * @brief 查找最接近的客户端支持的时区
 *
 * @details 魔兽世界客户端仅支持有限的时区列表,此函数用于将任意时区映射到
 *          客户端支持的最接近时区。优先匹配时区名称,名称不匹配时则匹配偏移量。
 *
 * @param currentTimezone 当前时区名称(IANA格式)
 * @param currentTimezoneOffset 当前时区偏移量(分钟)
 *
 * @return std::string_view 客户端支持的最接近的时区名称
 *
 * @note 客户端支持的时区包括:
 *   - 美洲: Los_Angeles, Denver, Chicago, New_York, Sao_Paulo
 *   - 欧洲: Paris
 *   - 亚洲: Shanghai, Taipei, Seoul
 *   - 大洋洲: Melbourne
 *   - 通用: UTC
 *
 * @code
 * // 示例: 当前时区为"Asia/Hong_Kong",偏移为480分钟
 * auto tz = FindClosestClientSupportedTimezone("Asia/Hong_Kong", 480min);
 * // 返回 "Asia/Shanghai" (名称不同但偏移相同)
 * @endcode
 */
TC_COMMON_API std::string_view FindClosestClientSupportedTimezone(std::string_view currentTimezone, Minutes currentTimezoneOffset);
}

#endif // TRINITYCORE_TIMEZONE_H
