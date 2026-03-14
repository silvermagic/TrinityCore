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
 * @file Locales.cpp
 * @brief 本地化区域设置管理模块
 *
 * 本模块负责初始化和管理 TrinityCore 的本地化设置，包括：
 * - 全局 UTF-8 区域设置，确保字符串处理的国际化支持
 * - 日历区域设置，用于日期和时间格式化
 * - C 运行时库的区域设置，保证数值格式的兼容性
 *
 * 通过统一管理区域设置，确保服务器在不同语言环境下正确处理文本、
 * 数值和日期时间数据。
 */

#include "Locales.h"
#include <boost/locale/generator.hpp>

namespace
{
/**
 * @brief 全局区域设置对象
 *
 * 存储全局 UTF-8 区域设置，用于所有需要本地化处理的字符串操作。
 * 该设置保留了经典 C 区域设置的数值格式，确保数字解析的一致性。
 */
std::locale _global;

/**
 * @brief 日历区域设置对象
 *
 * 专门用于日期和时间格式化的区域设置。
 * 由 Boost.Locale 生成，支持多语言日期时间显示。
 */
std::locale _calendar;
}

/**
 * @brief 初始化本地化区域设置
 *
 * 该函数在服务器启动时调用，完成以下初始化工作：
 * 1. 将全局区域设置从默认的 "C" 切换到 UTF-8，支持国际化字符处理
 * 2. 保留数值格式使用经典 C 风格，确保配置文件和数据交换的兼容性
 * 3. 同步 C 运行时库的区域设置，使 printf/scanf 等函数行为一致
 * 4. 初始化日历区域设置，用于时间格式化功能
 *
 * @note 此函数必须在使用任何本地化功能之前调用，通常在服务器启动流程中执行
 * @note 数值格式保持 "C" 风格是为了确保浮点数使用点号作为小数点，
 *       而不是某些区域使用逗号，这会影响配置文件解析和数据存储
 */
void Trinity::Locale::Init()
{
    // 创建系统默认的 UTF-8 区域设置
    // 空字符串 "" 表示使用系统环境变量指定的区域设置
    std::locale utf8("");

    // 保存 UTF-8 区域设置作为基础
    _global = utf8;

    // 使用 facets 组合技术：保留经典 C 区域设置的数值格式
    // 这样可以在支持 UTF-8 字符串的同时，确保数值格式的一致性
    // std::locale::numeric 表示只替换数值相关的 facet
    _global = std::locale(_global, std::locale::classic(), std::locale::numeric);

    // 将组合后的区域设置设为全局默认
    // 这会影响后续所有 std::locale 对象的默认构造
    std::locale::global(_global);

    // 设置 C 运行时库的全局区域设置
    // 这影响 printf、scanf、strtod 等 C 函数的行为
    std::setlocale(LC_ALL, "");

    // 强制将数值处理类别设回 "C" 区域
    // 这确保浮点数使用点号 (.) 作为小数点分隔符
    // 而不是某些区域（如德语）使用的逗号 (,)
    std::setlocale(LC_NUMERIC, "C");

    // 使用 Boost.Locale 生成日历专用的区域设置
    // Boost.Locale 提供比标准库更强大的国际化支持
    // 包括日历、时区、消息目录等功能
    boost::locale::generator g;
    _calendar = g.generate(utf8, "");
}

/**
 * @brief 获取全局区域设置
 *
 * 返回服务器使用的全局区域设置对象。该区域设置：
 * - 支持 UTF-8 编码的字符串处理
 * - 数值格式使用经典 C 风格（点号作为小数点）
 *
 * @return 全局区域设置对象的常量引用
 *
 * @note 返回常量引用避免拷贝开销，同时防止外部修改
 */
std::locale const& Trinity::Locale::GetGlobalLocale()
{
    return _global;
}

/**
 * @brief 获取日历区域设置
 *
 * 返回用于日期和时间格式化的区域设置对象。
 * 该区域设置由 Boost.Locale 生成，支持：
 * - 多语言日期时间格式
 * - 时区转换
 * - 日历计算（如工作日、月份名称等）
 *
 * @return 日历区域设置对象的常量引用
 *
 * @note 返回常量引用避免拷贝开销，同时防止外部修改
 */
std::locale const& Trinity::Locale::GetCalendarLocale()
{
    return _calendar;
}
