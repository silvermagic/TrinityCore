/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information

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
 * @file Locales.h
 * @brief 区域设置管理模块
 *
 * 本文件提供了 TrinityCore 的区域设置（Locale）管理功能。
 * 区域设置用于处理不同地区和语言的本地化需求，包括：
 * - 文本格式化（日期、时间、数字等）
 * - 字符串比较和排序规则
 * - 日历相关的本地化处理
 *
 * 该模块为服务器提供统一的区域设置访问接口，确保文本处理的一致性。
 */

#ifndef TRINITYCORE_LOCALE_H
#define TRINITYCORE_LOCALE_H

#include "Define.h"
#include <locale>

namespace Trinity::Locale
{
/**
 * @brief 初始化区域设置
 *
 * 初始化全局区域设置和日历区域设置。此函数应在服务器启动时调用，
 * 为后续的区域设置查询建立必要的环境。
 *
 * 该函数会根据系统环境和配置设置适当的默认区域设置，
 * 确保后续的文本处理和格式化操作能够正常工作。
 */
TC_COMMON_API void Init();

/**
 * @brief 获取全局区域设置
 *
 * 返回全局使用的区域设置对象，用于常规的文本处理和格式化操作。
 * 该区域设置在 Init() 函数中初始化，并在整个服务器生命周期中保持不变。
 *
 * @return 返回全局区域设置对象的常量引用
 *
 * @note 返回的引用在程序运行期间始终有效
 * @note 线程安全：可以安全地从多个线程读取该区域设置
 */
TC_COMMON_API std::locale const& GetGlobalLocale();

/**
 * @brief 获取日历区域设置
 *
 * 返回专门用于日历相关操作的区域设置对象。
 * 日历区域设置可能包含特定的时区和日期格式配置，
 * 用于处理游戏中的时间显示、日程安排等功能。
 *
 * @return 返回日历区域设置对象的常量引用
 *
 * @note 返回的引用在程序运行期间始终有效
 * @note 线程安全：可以安全地从多个线程读取该区域设置
 */
TC_COMMON_API std::locale const& GetCalendarLocale();
}

#endif // TRINITYCORE_LOCALE_H
