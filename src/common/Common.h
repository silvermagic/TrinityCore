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
 * @file Common.h
 * @brief TrinityCore 公共头文件 - 定义通用常量、枚举类型和工具宏
 *
 * 本文件提供了 TrinityCore 项目中广泛使用的公共定义，包括：
 * - 时间常量枚举
 * - 账户权限等级定义
 * - 本地化支持常量
 * - 数学常量（圆周率等）
 * - 调试相关变量
 *
 * 这些定义被项目的所有模块共享，是基础架构的核心组成部分。
 * 该文件中的定义独立于平台，可在所有支持的操作系统上使用。
 *
 * @note 本文件应包含在任何需要访问核心常量和枚举的源文件中
 * @warning 不要在此文件中添加特定于某个模块的定义
 */

#ifndef TRINITYCORE_COMMON_H
#define TRINITYCORE_COMMON_H

#include "Define.h"
#include <array>
#include <string>
#include <vector>

/**
 * @def STRINGIZE(a)
 * @brief 将宏参数转换为字符串字面量
 *
 * 该宏使用预处理器字符串化操作符 (#)，将任意宏参数转换为其字符串表示。
 * 例如：STRINGIZE(hello) 会被展开为 "hello"
 *
 * @param a 要转换为目标字符串的宏参数
 * @return 参数的字符串字面量表示
 *
 * @note 常用于在编译时生成调试信息或配置字符串
 */
#define STRINGIZE(a) #a

/**
 * @enum TimeConstants
 * @brief 时间常量枚举 - 定义常用的时间单位换算常量（秒和毫秒）
 *
 * 该枚举定义了游戏服务器中常用的时间单位换算常量，主要用于：
 * - 定时器设置
 * - 冷却时间计算
 * - 周期性任务调度
 * - 时间间隔判断
 *
 * 所有时间值均以秒（s）为单位，IN_MILLISECONDS 用于将秒转换为毫秒。
 *
 * @note MONTH（月）按30天计算，YEAR（年）按360天计算，这是游戏服务器的标准简化处理
 * @warning 使用这些常量时要注意单位的正确性（秒 vs 毫秒）
 *
 * 示例用法：
 * @code
 * uint32 cooldownTime = 5 * MINUTE;  // 5分钟冷却时间（秒）
 * uint32 durationMs = 2 * HOUR * IN_MILLISECONDS;  // 2小时转换为毫秒
 * @endcode
 */
enum TimeConstants
{
    MINUTE          = 60,                                   ///< 一分钟的秒数
    HOUR            = MINUTE*60,                            ///< 一小时的秒数 (3600秒)
    DAY             = HOUR*24,                              ///< 一天的秒数 (86400秒)
    WEEK            = DAY*7,                                ///< 一周的秒数 (604800秒)
    MONTH           = DAY*30,                               ///< 一月的秒数 (2592000秒，按30天计算)
    YEAR            = MONTH*12,                             ///< 一年的秒数 (31104000秒，按360天计算)
    IN_MILLISECONDS = 1000                                  ///< 毫秒转换因子，用于秒到毫秒的转换
};

/**
 * @enum AccountTypes
 * @brief 账户权限等级枚举 - 定义不同的账户访问权限级别
 *
 * 该枚举定义了 TrinityCore 服务器中的账户权限等级体系，用于：
 * - 控制命令访问权限
 * - 限制管理功能的使用
 * - 区分不同类型的用户角色
 * - 实现权限检查和验证
 *
 * 权限等级按数值递增排列，数值越高权限越大。SEC_CONSOLE 必须始终是最后一个值，
 * 它代表服务器控制台的权限级别（拥有最高权限）。
 *
 * @note 新增权限等级时应在现有等级后插入，保持 SEC_CONSOLE 在最后
 * @warning 权限检查时应使用 >= 比较而非 ==，以支持权限继承
 *
 * 示例用法：
 * @code
 * // 检查玩家是否有GM权限
 * if (player->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
 * {
 *     // 执行GM专属操作
 * }
 * @endcode
 */
enum AccountTypes
{
    SEC_PLAYER         = 0,                                 ///< 普通玩家权限 - 标准游戏功能访问
    SEC_MODERATOR      = 1,                                 ///< 版主权限 - 拥有基础管理能力（踢人、禁言等）
    SEC_GAMEMASTER     = 2,                                 ///< 游戏管理员(GM)权限 - 中等管理功能
    SEC_ADMINISTRATOR  = 3,                                 ///< 管理员权限 - 高级管理功能
    SEC_CONSOLE        = 4                                  ///< 控制台权限 - 服务器控制台的最高权限（必须始终在最后）
};

/**
 * @enum LocaleConstant
 * @brief 本地化常量枚举 - 定义游戏支持的语言区域代码
 *
 * 该枚举定义了 TrinityCore 支持的所有语言区域，用于：
 * - 客户端/服务器语言协商
 * - 多语言内容加载（任务文本、物品名称等）
 * - 本地化字符串查找
 * - 国际化（i18n）支持
 *
 * 每个区域代码遵循 [语言代码][地区代码] 的命名规范：
 * - 前两个字母：ISO 639-1 语言代码（如 en=英语, zh=中文）
 * - 后两个字母：ISO 3166-1 国家/地区代码（如 US=美国, CN=中国大陆, TW=台湾）
 *
 * @note 枚举值与客户端的语言设置一一对应，不应随意修改
 * @warning TOTAL_LOCALES 用于数组大小定义，新增语言时需更新
 * @see localeNames 用于将枚举转换为字符串名称
 * @see GetLocaleByName 用于从字符串获取枚举值
 */
enum LocaleConstant : uint8
{
    LOCALE_enUS = 0,                                        ///< 英语（美国） - English (United States)
    LOCALE_koKR = 1,                                        ///< 韩语（韩国） - Korean (Korea)
    LOCALE_frFR = 2,                                        ///< 法语（法国） - French (France)
    LOCALE_deDE = 3,                                        ///< 德语（德国） - German (Germany)
    LOCALE_zhCN = 4,                                        ///< 简体中文（中国大陆） - Simplified Chinese (China)
    LOCALE_zhTW = 5,                                        ///< 繁体中文（台湾） - Traditional Chinese (Taiwan)
    LOCALE_esES = 6,                                        ///< 西班牙语（西班牙） - Spanish (Spain)
    LOCALE_esMX = 7,                                        ///< 西班牙语（墨西哥） - Spanish (Mexico)
    LOCALE_ruRU = 8,                                        ///< 俄语（俄罗斯） - Russian (Russia)

    TOTAL_LOCALES                                           ///< 本地化区域总数，用于数组定义
};

/**
 * @def DEFAULT_LOCALE
 * @brief 默认语言区域设置
 *
 * 当客户端未指定语言或语言无效时使用的默认语言区域。
 * 默认设置为美式英语（LOCALE_enUS），这是 WoW 客户端的基准语言。
 */
#define DEFAULT_LOCALE LOCALE_enUS

/**
 * @def MAX_LOCALES
 * @brief 支持的最大语言区域数量
 *
 * 该宏定义了服务器支持的最大语言数量。虽然 TOTAL_LOCALES 提供了实际的本地化数量，
 * 但 MAX_LOCALES 用于固定大小的数组定义和循环限制，以确保向后兼容性。
 *
 * @note 当前值为 8，对应 WoW 3.3.5 客户端支持的语言数量
 */
#define MAX_LOCALES 8

/**
 * @def MAX_ACCOUNT_TUTORIAL_VALUES
 * @brief 账户教程数据数组的最大容量
 *
 * 定义了每个账户存储教程进度数据的数组大小。
 * 教程系统用于引导新玩家了解游戏机制和界面。
 */
#define MAX_ACCOUNT_TUTORIAL_VALUES 8

/**
 * @var localeNames
 * @brief 语言区域名称字符串数组
 *
 * 该数组存储了所有支持的语言区域的字符串名称，与 LocaleConstant 枚举一一对应。
 * 索引值即为 LocaleConstant 枚举值，数组元素为对应的语言代码字符串。
 *
 * 用途：
 * - 将 LocaleConstant 枚举转换为可读的字符串标识
 * - 日志输出和调试信息
 * - 配置文件解析
 *
 * @note 数组大小为 TOTAL_LOCALES，由 Common.cpp 提供定义
 * @see GetLocaleByName 用于反向查找（从字符串获取枚举值）
 */
TC_COMMON_API extern char const* localeNames[TOTAL_LOCALES];

/**
 * @fn GetLocaleByName
 * @brief 根据语言名称字符串获取对应的本地化枚举值
 *
 * 该函数将语言名称字符串（如 "enUS", "zhCN"）转换为 LocaleConstant 枚举值。
 * 如果提供的名称无效或不在支持列表中，则返回默认语言 LOCALE_enUS。
 *
 * @param name 语言区域名称字符串（如 "enUS", "zhCN", "deDE" 等）
 * @return 对应的 LocaleConstant 枚举值，无效名称返回 DEFAULT_LOCALE
 *
 * @note 该函数执行不区分大小写的比较
 * @see localeNames 语言名称字符串数组
 *
 * 示例用法：
 * @code
 * LocaleConstant locale = GetLocaleByName("zhCN");  // 返回 LOCALE_zhCN
 * LocaleConstant invalid = GetLocaleByName("xxXX"); // 返回 LOCALE_enUS（默认值）
 * @endcode
 */
TC_COMMON_API LocaleConstant GetLocaleByName(std::string const& name);

/**
 * @defgroup MathConstants 数学常量定义
 * @brief 提供常用的数学常量，用于游戏中的几何计算
 * @{
 */

#ifndef M_PI
/**
 * @def M_PI
 * @brief 圆周率 π 的值
 *
 * 定义圆周率常量，精确到小数点后 20 位。
 * 用于游戏中的角度/弧度转换、圆形运动计算、方向向量计算等。
 *
 * 应用场景：
 * - 角度与弧度的相互转换（角度 × π/180 = 弧度）
 * - 计算圆形区域的坐标
 * - 旋转和面向角度计算
 * - 3D 空间中的数学运算
 *
 * @note 某些平台的标准数学库已定义此宏，使用 #ifndef 避免重定义冲突
 *
 * 示例用法：
 * @code
 * float radians = degrees * M_PI / 180.0f;  // 角度转弧度
 * float x = radius * cos(angle);            // 圆形运动计算
 * @endcode
 */
#define M_PI 3.14159265358979323846
#endif

#ifndef M_PI_4
/**
 * @def M_PI_4
 * @brief 四分之一圆周率 (π/4) 的值
 *
 * 定义 π/4 的常量值，等于 45 度角的弧度值。
 * 主要用于某些特定的数学计算优化和角度变换。
 *
 * @note 某些平台的标准数学库已定义此宏，使用 #ifndef 避免重定义冲突
 */
#define M_PI_4 0.785398163397448309616
#endif

/** @} */ // MathConstants 结束

/**
 * @def MAX_QUERY_LEN
 * @brief 数据库查询语句的最大长度限制
 *
 * 定义数据库查询 SQL 语句的最大长度（32KB）。
 * 该限制用于防止过长的查询语句导致缓冲区溢出或性能问题。
 *
 * @note 32KB (32*1024 = 32768 字节) 对于绝大多数查询已经足够
 * @warning 构建动态 SQL 语句时应检查长度是否超过此限制
 */
#define MAX_QUERY_LEN 32*1024

/**
 * @defgroup DebugVariables 调试变量
 * @brief 用于调试法术系统的全局变量
 *
 * 这些变量用于开发和调试阶段的法术系统追踪。
 * 通过配置这些变量，可以针对特定法术启用详细的调试日志输出。
 * @{
 */

/**
 * @var debugSpellIds
 * @brief 需要调试的法术 ID 列表
 *
 * 该向量存储了需要启用调试输出的法术 ID。
 * 当法术系统处理这些 ID 的法术时，会输出详细的调试信息，
 * 帮助开发者追踪法术执行流程、参数传递和状态变化。
 *
 * 用途：
 * - 追踪特定法术的执行流程
 * - 调试法术效果和光环应用
 * - 分析法术伤害计算和命中判定
 * - 排查法术相关的 Bug
 *
 * @note 生产环境中应为空向量以避免性能影响
 * @see debugSpellSeqId 用于序列匹配的调试标识
 */
TC_COMMON_API extern std::vector<uint32> debugSpellIds;

/**
 * @var debugSpellSeqId
 * @brief 法术调试序列标识符
 *
 * 该变量用于法术调试时的序列追踪，帮助识别和关联
 * 特定的法术施放事件。在调试输出中会显示此序列 ID，
 * 便于在日志中过滤和追踪特定的法术实例。
 *
 * @note 主要用于开发阶段的调试和测试
 */
TC_COMMON_API extern uint32 debugSpellSeqId;

/** @} */ // DebugVariables 结束

#endif // TRINITYCORE_COMMON_H
