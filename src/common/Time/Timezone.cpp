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
 * @file Timezone.cpp
 * @brief 时区处理模块实现
 *
 * @details 本文件实现了跨平台的时区查询和转换功能,主要包括:
 *   - 时区偏移哈希数据库的构建与查询
 *   - 系统时区信息的获取(名称和偏移量)
 *   - 客户端兼容时区的匹配算法
 *
 * 核心数据结构:
 *   - 时区哈希映射表: 哈希值 -> 偏移量(分钟)
 *   - 客户端支持的时区列表: 11个主要游戏区域时区
 *
 * 时区转换原理:
 *   1. 本地时间 = UTC时间 + 时区偏移量
 *   2. 时区偏移量会受夏令时(DST)影响而变化
 *   3. 客户端使用FNV-1a哈希算法对偏移字符串进行哈希
 *   4. 服务器通过反向查表获取实际偏移量
 *
 * 平台实现差异:
 *   - Windows平台:
 *     * 使用C++20 <chrono>时区库
 *     * 动态构建时区哈希表,遍历系统时区数据库
 *     * 支持 std::chrono::time_zone 和相关API
 *
 *   - 非Windows平台(Linux/macOS):
 *     * 使用预生成的时区哈希表(覆盖UTC-12到UTC+14)
 *     * 使用POSIX localtime_r获取时区偏移(tm_gmtoff字段)
 *     * 使用boost::locale获取时区名称
 *
 * 使用场景:
 *   1. 客户端认证处理: 解析客户端发送的时区哈希值
 *   2. 时间同步: 将服务器时间转换为玩家本地时间
 *   3. 日志记录: 根据时区偏移显示本地时间戳
 *
 * 性能优化:
 *   - 使用静态局部变量实现单例模式的时区哈希表
 *   - 延迟初始化,仅在首次使用时构建
 *   - 使用unordered_map实现O(1)查找性能
 *
 * @see Timezone.h 头文件声明
 */

#include "Timezone.h"
#include "Hash.h"              // FNV-1a哈希算法实现
#include "Locales.h"           // 本地化支持
#include "MapUtils.h"          // MapGetValuePtr工具函数
#include "StringConvert.h"     // 字符串转换工具
#include "Util.h"              // TimeBreakdown等通用工具
#include <boost/locale/date_time_facet.hpp>  // boost日期时间facet
#include <chrono>              // C++11时间库
#include <memory>              // 智能指针
#include <unordered_map>       // 哈希映射容器

namespace
{
/**
 * @brief 初始化时区哈希数据库
 *
 * 职责:
 *   创建一个从哈希值到时区偏移量的映射表,用于快速查找时区偏移
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   std::unordered_map<uint32, Minutes, std::identity> - 哈希值到分钟偏移量的映射表
 *
 * 主要流程:
 *   Windows平台:
 *     1. 遍历系统时区数据库中的所有时区
 *     2. 获取每个时区的偏移量(分钟)
 *     3. 将偏移量转为字符串并计算FNV-1a哈希值
 *     4. 建立哈希值到偏移量的映射
 *
 *   非Windows平台:
 *     使用预生成的时区偏移哈希表(包含-720到840分钟的各个时区)
 *     这些值对应世界各地的主要时区偏移量
 */
std::unordered_map<uint32, Minutes, std::identity> InitTimezoneHashDb()
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS

    // Windows平台: 动态构建时区哈希数据库
    // 目的: 生成哈希数据库以匹配客户端认证包中发送的值
    std::unordered_map<uint32, Minutes, std::identity> hashToOffset;
    std::chrono::system_clock::time_point dummmy;  // 临时时间点,用于获取时区信息

    // 遍历系统时区数据库中的所有时区
    for (std::chrono::time_zone const& zone : std::chrono::get_tzdb().zones)
    {
        // 获取该时区的系统信息(包含偏移量)
        std::chrono::sys_info sysInfo = zone.get_info(dummmy);
        // 将秒级偏移转换为分钟级偏移
        Minutes offsetMinutes = std::chrono::duration_cast<Minutes>(sysInfo.offset);
        // 将偏移量转换为字符串
        std::string offsetStr = Trinity::ToString(offsetMinutes.count());
        // 计算FNV-1a哈希并建立映射关系
        hashToOffset.emplace(Trinity::HashFnv1a(offsetStr), offsetMinutes);
    }

#else
    // 非Windows平台: 使用预生成的时区偏移哈希表
    // 说明: 为不支持C++20时区API的编译器提供预计算的数据
    // 范围: UTC-12 (-720分钟) 到 UTC+14 (840分钟)
    // 格式: { FNV-1a哈希值, 偏移量(分钟) }
    std::unordered_map<uint32, Minutes, std::identity> hashToOffset =
    {
        { 0xAADC2D37u, -720min },   // UTC-12: 贝克岛、豪兰岛
        { 0x362F107Bu, -690min },   // UTC-11:30 (特殊时区)
        { 0x2C44C70Cu, -660min },   // UTC-11: 美属萨摩亚、纽埃岛
        { 0xB84A209Eu, -640min },   // UTC-10:40 (特殊时区)
        { 0xBA3D57D1u, -630min },   // UTC-10:30 (特殊时区)
        { 0x4040695Au, -600min },   // UTC-10: 夏威夷、塔希提岛
        { 0xB65A75D0u, -570min },   // UTC-9:30 马克萨斯群岛
        { 0xC8614DEBu, -540min },   // UTC-9: 阿拉斯加、甘比尔群岛
        { 0x3A68BD26u, -510min },   // UTC-8:30 (特殊时区)
        { 0x51E8096Cu, -480min },   // UTC-8: 太平洋标准时间 (洛杉矶、温哥华)
        { 0x4DD8F896u, -420min },   // UTC-7: 山地标准时间 (丹佛、亚利桑那)
        { 0x674B7C0Fu, -360min },   // UTC-6: 中部标准时间 (芝加哥、墨西哥城)
        { 0x633C6B39u, -300min },   // UTC-5: 东部标准时间 (纽约、多伦多)
        { 0x0BAD340Au, -240min },   // UTC-4: 大西洋标准时间 (哈利法克斯、加拉加斯)
        { 0x74B25683u, -225min },   // UTC-3:45 委内瑞拉标准时间
        { 0x09B9FCD7u, -210min },   // UTC-3:30 纽芬兰标准时间
        { 0x150C169Bu, -180min },   // UTC-3: 巴西利亚、布宜诺斯艾利斯
        { 0x191B2771u, -120min },   // UTC-2: 中大西洋时间
        { 0xD7D3B14Eu, -60min },    // UTC-1: 亚速尔群岛、佛得角
        { 0x47CE5170u, -44min },    // UTC-0:44 (历史时区)
        { 0x350CA8AFu, 0min },      // UTC+0: 协调世界时 (伦敦、都柏林、里斯本)
        { 0x15E8E23Bu, 60min },     // UTC+1: 中欧时间 (巴黎、柏林、罗马)
        { 0x733864AEu, 120min },    // UTC+2: 东欧时间 (开罗、赫尔辛基、雅典)
        { 0xF71F9C94u, 180min },    // UTC+3: 莫斯科时间 (莫斯科、伊斯坦布尔)
        { 0xBDE50F54u, 210min },    // UTC+3:30 德黑兰时间
        { 0x2BDD6DB9u, 240min },    // UTC+4: 海湾标准时间 (迪拜、巴库)
        { 0xB1E07F42u, 270min },    // UTC+4:30 阿富汗时间
        { 0x454FF132u, 300min },    // UTC+5: 巴基斯坦时间 (卡拉奇、塔什干)
        { 0x3F4DA929u, 330min },    // UTC+5:30 印度标准时间 (新德里、孟买)
        { 0xD1554AC4u, 360min },    // UTC+6: 孟加拉时间 (达卡、阿拉木图)
        { 0xBB667143u, 390min },    // UTC+6:30 缅甸时间 (仰光)
        { 0x9E2B78C9u, 420min },    // UTC+7: 印支时间 (曼谷、雅加达)
        { 0x1C377816u, 450min },    // UTC+7:30 (历史时区)
        { 0x1A4440E3u, 480min },    // UTC+8: 中国标准时间 (北京、上海、台北)
        { 0xB49DF789u, 525min },    // UTC+8:45 澳大利亚中西部时间
        { 0xC3A28C54u, 540min },    // UTC+9: 韩国标准时间 (首尔、东京)
        { 0x35A9FB8Fu, 570min },    // UTC+9:30 澳大利亚中部时间 (达尔文)
        { 0x889BD751u, 600min },    // UTC+10: 澳大利亚东部时间 (墨尔本、悉尼)
        { 0x8CAAE827u, 660min },    // UTC+11: 所罗门群岛时间
        { 0x7285EE60u, 690min },    // UTC+11:30 诺福克岛时间
        { 0x1CC2DEF4u, 720min },    // UTC+12: 新西兰时间 (奥克兰、斐济)
        { 0x89B8FD2Fu, 765min },    // UTC+12:45 查塔姆群岛时间
        { 0x98DBA70Eu, 780min },    // UTC+13: 萨摩亚时间
        { 0xC59585BBu, 840min }     // UTC+14: 莱恩群岛时间 (基里巴斯)
    };
#endif

    return hashToOffset;
}

/**
 * @brief 获取时区偏移哈希映射表
 *
 * 职责:
 *   返回全局单例的时区哈希映射表,延迟初始化
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   const std::unordered_map<uint32, Minutes, std::identity>& - 哈希值到分钟偏移量的常量引用
 *
 * 主要流程:
 *   1. 使用静态局部变量实现单例模式
 *   2. 首次调用时通过InitTimezoneHashDb()初始化
 *   3. 后续调用直接返回已初始化的映射表
 *
 * 设计模式:
 *   使用Meyers单例模式(静态局部变量):
 *   - 线程安全(C++11保证)
 *   - 延迟初始化
 *   - 无需手动内存管理
 */
std::unordered_map<uint32, Minutes, std::identity> const& GetTimezoneOffsetsByHash()
{
    // 静态局部变量: 程序生命周期内只初始化一次
    static std::unordered_map<uint32, Minutes, std::identity> timezoneMap = InitTimezoneHashDb();
    return timezoneMap;
}

/**
 * @brief 客户端支持的时区类型定义
 *
 * @details 定义一个时区条目的数据结构
 *   - first: 时区偏移量(分钟)
 *   - second: IANA时区名称
 */
using ClientSupportedTimezone = std::pair<Minutes, std::string>;

/**
 * @brief 客户端支持的时区列表
 *
 * 职责:
 *   定义魔兽世界客户端支持的11个主要时区
 *
 * 说明:
 *   这些时区涵盖了主要的游戏区域:
 *   - 美洲: 洛杉矶(PST)、丹佛(MST)、芝加哥(CST)、纽约(EST)、圣保罗
 *   - 欧洲: 巴黎(CET)
 *   - 亚洲: 上海(CST)、台北(CST)、首尔(KST)
 *   - 大洋洲: 墨尔本(AEST)
 *   - 通用: UTC
 *
 * 数据来源:
 *   魔兽世界客户端内置的时区列表,用于:
 *   - 服务器时间到本地时间的转换
 *   - 时间相关的游戏事件调度
 *   - 客户端认证时的时区协商
 *
 * 注意:
 *   - 列表固定不变,由客户端版本决定
 *   - 使用标准IANA时区标识符格式
 *   - 每个时区的偏移量是标准时间,不含夏令时
 */
std::array<ClientSupportedTimezone, 11> const _clientSupportedTimezones =
{{
    { -480min, "America/Los_Angeles" },   // 太平洋标准时间 (PST) - 美国西海岸
    { -420min, "America/Denver" },        // 山地标准时间 (MST) - 美国山地地区
    { -360min, "America/Chicago" },       // 中部标准时间 (CST) - 美国中部
    { -300min, "America/New_York" },      // 东部标准时间 (EST) - 美国东海岸
    { -180min, "America/Sao_Paulo" },     // 巴西利亚时间 (BRT) - 巴西
    {    0min, "Etc/UTC" },               // 协调世界时 (UTC) - 通用标准时间
    {   60min, "Europe/Paris" },          // 中欧时间 (CET) - 欧洲中部
    {  480min, "Asia/Shanghai" },         // 中国标准时间 (CST) - 中国大陆
    {  480min, "Asia/Taipei" },           // 台北时间 (CST) - 台湾地区
    {  540min, "Asia/Seoul" },            // 韩国标准时间 (KST) - 韩国
    {  600min, "Australia/Melbourne" },   // 澳大利亚东部标准时间 (AEST) - 澳大利亚东部
}};
}

namespace Trinity::Timezone
{
/**
 * @brief 根据哈希值获取时区偏移量
 *
 * 职责:
 *   通过哈希值查找对应的时区偏移量(分钟)
 *
 * 参数:
 *   hash - 时区偏移量字符串的FNV-1a哈希值
 *
 * 返回值:
 *   Minutes - 时区偏移量(分钟),如果未找到则返回0
 *
 * 主要流程:
 *   1. 在全局时区哈希映射表中查找指定哈希值
 *   2. 如果找到则返回对应的偏移量
 *   3. 如果未找到则返回0分钟(UTC时区)
 *
 * 使用场景:
 *   客户端认证阶段,解析客户端发送的时区哈希值
 */
Minutes GetOffsetByHash(uint32 hash)
{
    // 在哈希映射表中查找,MapGetValuePtr返回指针避免拷贝
    if (Minutes const* offset = Containers::MapGetValuePtr(GetTimezoneOffsetsByHash(), hash))
        return *offset;

    // 未找到则返回UTC时区(偏移0分钟)
    return 0min;
}

/**
 * @brief 获取系统时区在指定时间点的偏移量
 *
 * 职责:
 *   计算系统当前时区在指定时间点相对于UTC的偏移量
 *
 * 参数:
 *   date - 要查询的系统时间点
 *
 * 返回值:
 *   Minutes - 时区偏移量(分钟)
 *
 * 主要流程:
 *   Windows平台:
 *     1. 使用C++20时区库获取当前时区
 *     2. 查询指定时间点的偏移信息
 *
 *   非Windows平台:
 *     1. 将时间点转换为time_t
 *     2. 使用本地时间分解获取tm_gmtoff字段
 *     3. 该字段包含相对于UTC的偏移秒数
 *
 * 注意:
 *   - 不同平台使用不同的API获取时区信息
 *   - 返回值以分钟为单位,会进行类型转换
 *   - 偏移量包含夏令时调整(如适用)
 */
Minutes GetSystemZoneOffsetAt(SystemTimePoint date)
{
    Seconds offset;
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Windows平台: 使用C++20 chrono时区库
    // current_zone()返回当前时区对象
    // get_info()获取指定时间点的时区信息(包含偏移量)
    offset = std::chrono::current_zone()->get_info(date).offset;
#else
    // 非Windows平台: 使用POSIX时间函数
    // 将系统时间点转换为time_t(Unix时间戳)
    tm buf = TimeBreakdown(std::chrono::system_clock::to_time_t(date));
    // tm_gmtoff字段存储了相对于UTC的偏移秒数(包含夏令时)
    offset = Seconds(buf.tm_gmtoff);
#endif
    // 将秒级偏移转换为分钟级偏移
    return std::chrono::duration_cast<Minutes>(offset);
}

/**
 * @brief 获取系统时区偏移量
 *
 * 职责:
 *   获取系统当前时区相对于UTC的偏移量,可选择是否考虑夏令时
 *
 * 参数:
 *   applyDst - 是否应用夏令时调整(默认为true)
 *             - true: 使用当前时间,包含夏令时调整
 *             - false: 使用Unix纪元时间,不包含夏令时
 *
 * 返回值:
 *   Minutes - 时区偏移量(分钟)
 *
 * 主要流程:
 *   1. 如果applyDst为true:
 *      - 使用当前时间点,包含夏令时调整
 *   2. 如果applyDst为false:
 *      - 使用Unix纪元时间(time_t(0)),此时不包含夏令时
 *   3. 调用GetSystemZoneOffsetAt计算指定时间点的偏移量
 *
 * 设计原理:
 *   - Unix纪元时间(1970-01-01 00:00:00 UTC)通常不受夏令时影响
 *   - 大多数时区在该时间点使用标准时间
 *   - 通过这种方式可以获取标准时区偏移(不含夏令时)
 *
 * 注意:
 *   - 夏令时会影响时区偏移量(通常增加1小时)
 *   - 使用time_t(0)作为基准时间可以获取标准时区偏移
 */
Minutes GetSystemZoneOffset(bool applyDst /*= true*/)
{
    // 默认使用Unix纪元时间(time_t(0))获取标准偏移(不含夏令时)
    std::chrono::system_clock::time_point date = std::chrono::system_clock::from_time_t(std::time_t(0));

    if (applyDst)
    {
        // 如果需要夏令时,使用当前时间点
        // 此时计算的偏移量会包含夏令时调整(如适用)
        date = std::chrono::system_clock::now();
    }

    return GetSystemZoneOffsetAt(date);
}

/**
 * @brief 获取系统时区名称
 *
 * 职责:
 *   返回系统当前时区的标准名称字符串
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   std::string - 时区名称(如"Asia/Shanghai"、"America/New_York"等)
 *
 * 主要流程:
 *   Windows平台:
 *     使用C++20时区库直接获取当前时区名称
 *
 *   非Windows平台:
 *     1. 通过boost::locale库获取日历facet
 *     2. 创建日历对象并获取其时区名称
 *
 * 时区名称格式:
 *   - 使用IANA时区数据库标识符
 *   - 格式: "区域/城市" (如"Asia/Shanghai")
 *   - 或特殊标识符 (如"Etc/UTC")
 *
 * 常见返回值示例:
 *   - "Asia/Shanghai": 中国大陆
 *   - "America/New_York": 美国东部
 *   - "Europe/Paris": 欧洲中部
 *   - "Etc/UTC": UTC时区
 *
 * 注意:
 *   - 返回的是IANA时区标识符格式
 *   - 不同平台使用不同的底层实现
 */
std::string GetSystemZoneName()
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Windows平台: 使用C++20 chrono时区库
    // current_zone()返回当前时区对象的指针
    // name()返回IANA格式的时区名称
    return std::string(std::chrono::current_zone()->name());
#else
    // 非Windows平台: 使用boost::locale库
    // 获取日历locale的facet
    std::unique_ptr<boost::locale::abstract_calendar> p(std::use_facet<class boost::locale::calendar_facet>(Locale::GetCalendarLocale()).create_calendar());
    // 从日历对象中提取时区名称
    return p->get_timezone();
#endif
}

/**
 * @brief 查找最接近的客户端支持的时区
 *
 * 职责:
 *   根据当前时区名称和偏移量,找到客户端支持的时区列表中最匹配的时区
 *
 * 参数:
 *   currentTimezone - 当前时区名称(如"Asia/Shanghai")
 *   currentTimezoneOffset - 当前时区偏移量(分钟)
 *
 * 返回值:
 *   std::string_view - 客户端支持的最接近的时区名称
 *
 * 匹配算法:
 *   1. 第一阶段: 精确名称匹配
 *      - 在客户端支持的时区列表中查找与currentTimezone完全相同的时区
 *      - 如果找到则立即返回,确保时区准确性
 *
 *   2. 第二阶段: 偏移量近似匹配
 *      - 如果名称不匹配,计算每个客户端支持时区与当前偏移量的差值
 *      - 选择差值绝对值最小的时区
 *      - 返回该时区名称
 *
 * 匹配优先级说明:
 *   - 精确名称匹配 > 偏移量匹配
 *   - 名称匹配可以确保时区规则完全一致(包括夏令时等)
 *   - 偏移量匹配可能选择不同地区但相同偏移的时区
 *
 * 使用场景:
 *   - 服务器向客户端发送时区信息时使用
 *   - 确保发送的时区在客户端支持的范围内
 *
 * 示例:
 *   输入: currentTimezone="Asia/Hong_Kong", currentTimezoneOffset=480min
 *   流程:
 *     1. 在客户端支持列表中查找"Asia/Hong_Kong" -> 未找到
 *     2. 查找偏移量480min最接近的时区
 *     3. 找到"Asia/Shanghai" (偏移480min)
 *   输出: "Asia/Shanghai"
 *
 * 注意:
 *   - 优先使用名称精确匹配,确保时区准确性
 *   - 偏移量匹配可以处理时区名称不同但偏移相同的情况
 *   - 例如"Asia/Shanghai"和"Asia/Taipei"都是UTC+8
 */
std::string_view FindClosestClientSupportedTimezone(std::string_view currentTimezone, Minutes currentTimezoneOffset)
{
    // 第一阶段: 尝试精确匹配时区名称
    // 使用std::find_if在客户端支持的时区列表中查找
    auto itr = std::find_if(_clientSupportedTimezones.begin(), _clientSupportedTimezones.end(), [currentTimezone](ClientSupportedTimezone const& tz)
    {
        // 比较时区名称字符串
        return tz.second == currentTimezone;
    });

    // 如果找到精确匹配,直接返回
    if (itr != _clientSupportedTimezones.end())
        return itr->second;

    // 第二阶段: 尝试匹配最接近的时区偏移量
    // 使用std::min_element找到偏移量差值最小的时区
    itr = std::min_element(_clientSupportedTimezones.begin(), _clientSupportedTimezones.end(), [currentTimezoneOffset](ClientSupportedTimezone const& left, ClientSupportedTimezone const& right)
    {
        // 计算左侧时区与当前偏移量的差值
        Minutes leftDiff = left.first - currentTimezoneOffset;
        // 计算右侧时区与当前偏移量的差值
        Minutes rightDiff = right.first - currentTimezoneOffset;
        // 比较差值的绝对值,选择更接近的
        return std::abs(leftDiff.count()) < std::abs(rightDiff.count());
    });

    // 返回偏移量最接近的时区名称
    return itr->second;
}
}
