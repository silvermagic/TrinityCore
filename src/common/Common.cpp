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
 * @file Common.cpp
 * @brief 通用工具函数实现文件
 *
 * 本文件实现了 TrinityCore 项目中使用的通用工具函数和全局变量，
 * 主要包括语言环境相关的工具函数和调试相关的全局变量。
 */

#include "Common.h"

/**
 * @brief 语言环境名称数组
 *
 * 存储所有支持的语言环境的字符串名称，用于语言环境常量与字符串名称之间的转换。
 * 数组索引与 LocaleConstant 枚举值对应。
 *
 * 支持的语言环境包括：
 * - enUS: 美国英语
 * - koKR: 韩语
 * - frFR: 法语
 * - deDE: 德语
 * - zhCN: 简体中文
 * - zhTW: 繁体中文（台湾）
 * - esES: 西班牙语
 * - esMX: 墨西哥西班牙语
 * - ruRU: 俄语
 */
char const* localeNames[TOTAL_LOCALES] =
{
  "enUS",
  "koKR",
  "frFR",
  "deDE",
  "zhCN",
  "zhTW",
  "esES",
  "esMX",
  "ruRU"
};

/**
 * @brief 根据语言环境名称获取语言环境常量
 *
 * @param name 语言环境名称字符串，如 "enUS"、"zhCN" 等
 * @return LocaleConstant 对应的语言环境常量枚举值
 *                         如果未找到匹配项，默认返回 LOCALE_enUS
 *
 * @details
 * 职责：
 *   将语言环境字符串名称转换为对应的枚举常量，便于在系统中统一使用。
 *
 * 主要流程：
 *   1. 遍历所有支持的语言环境名称数组
 *   2. 将输入名称与数组中的每个元素进行比较
 *   3. 如果找到匹配项，返回对应的语言环境常量
 *   4. 如果遍历完所有元素仍未找到匹配项，返回默认值 LOCALE_enUS
 *
 * 注意事项：
 *   - 该函数区分大小写，输入的名称必须与 localeNames 数组中的名称完全匹配
 *   - 对于 enGB（英国英语）等未显式支持的区域，也返回 LOCALE_enUS 作为默认值
 */
LocaleConstant GetLocaleByName(const std::string& name)
{
    for (uint32 i = 0; i < TOTAL_LOCALES; ++i)
        if (name == localeNames[i])
            return LocaleConstant(i);

    return LOCALE_enUS;                                     // including enGB case（包括 enGB 情况）
}

/**
 * @brief 调试技能ID列表
 *
 * 存储需要调试跟踪的技能ID集合。
 * 用于在调试模式下记录特定技能的执行信息。
 */
std::vector<uint32> debugSpellIds = {};

/**
 * @brief 调试技能序列ID
 *
 * 用于生成调试技能的唯一序列号标识。
 * 每次使用时递增，确保每个调试事件都有唯一的序列ID。
 */
uint32 debugSpellSeqId = 0;
