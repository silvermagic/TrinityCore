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
 * @file FieldValueConverter.h
 * @brief 数据库结果字段值转换器基类定义
 *
 * 本文件定义了数据库查询结果字段值转换器的抽象基类接口。
 * 该转换器负责将数据库原始数据（二进制格式）转换为 C++ 类型数据。
 *
 * 主要功能：
 * - 提供统一的数据库字段值转换接口
 * - 支持多种数据类型的转换（整数、浮点数、字符串、日期时间等）
 * - 处理数据截断和类型溢出的日志记录
 *
 * @note 该类为抽象基类，具体实现由不同的数据库驱动（如 MySQL、PostgreSQL）提供。
 * @see QueryResultFieldMetadata
 */

#ifndef TRINITY_FIELD_VALUE_CONVERTER_H
#define TRINITY_FIELD_VALUE_CONVERTER_H

#include "Define.h"
#include "Duration.h"

struct QueryResultFieldMetadata;

/**
 * @class BaseDatabaseResultValueConverter
 * @brief 数据库结果值转换器抽象基类
 *
 * 该类定义了数据库查询结果字段值转换的标准接口。
 * 所有具体的数据库驱动实现都必须继承此类并提供具体的转换逻辑。
 *
 * 设计模式：
 * - 使用策略模式，不同的数据库类型可以有不同的转换实现
 * - 禁止拷贝和移动，确保转换器的唯一性
 *
 * 使用场景：
 * - 当数据库查询返回结果时，需要将原始的二进制数据转换为 C++ 类型
 * - 处理不同数据库类型之间的数据格式差异
 *
 * 线程安全性：
 * - 转换操作应为线程安全的，可被多线程并发调用
 * - 实现类不应包含可变状态
 *
 * @note 所有 Get* 方法都是纯虚函数，必须由派生类实现
 */
class BaseDatabaseResultValueConverter
{
public:
    /**
     * @brief 默认构造函数
     *
     * 构造一个数据库结果值转换器基类实例。
     */
    BaseDatabaseResultValueConverter();

    /// 禁止拷贝构造
    BaseDatabaseResultValueConverter(BaseDatabaseResultValueConverter const&) = delete;
    /// 禁止移动构造
    BaseDatabaseResultValueConverter(BaseDatabaseResultValueConverter&&) = delete;
    /// 禁止拷贝赋值
    BaseDatabaseResultValueConverter& operator=(BaseDatabaseResultValueConverter const&) = delete;
    /// 禁止移动赋值
    BaseDatabaseResultValueConverter& operator=(BaseDatabaseResultValueConverter&&) = delete;

    /**
     * @brief 虚析构函数
     *
     * 确保派生类的析构函数能被正确调用，避免内存泄漏。
     */
    virtual ~BaseDatabaseResultValueConverter();

    /**
     * @brief 从数据库结果中获取 8 位无符号整数值
     *
     * 将数据库原始数据转换为 uint8 类型（0-255）。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息，用于错误报告
     * @return 转换后的 8 位无符号整数值
     *
     * @note 如果源数据超出 uint8 范围，可能会发生截断并记录日志
     * @see LogTruncation
     */
    virtual uint8 GetUInt8(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取 8 位有符号整数值
     *
     * 将数据库原始数据转换为 int8 类型（-128 到 127）。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的 8 位有符号整数值
     *
     * @note 如果源数据超出 int8 范围，可能会发生截断并记录日志
     */
    virtual int8 GetInt8(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取 16 位无符号整数值
     *
     * 将数据库原始数据转换为 uint16 类型（0-65535）。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的 16 位无符号整数值
     *
     * @note 如果源数据超出 uint16 范围，可能会发生截断并记录日志
     */
    virtual uint16 GetUInt16(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取 16 位有符号整数值
     *
     * 将数据库原始数据转换为 int16 类型（-32768 到 32767）。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的 16 位有符号整数值
     *
     * @note 如果源数据超出 int16 范围，可能会发生截断并记录日志
     */
    virtual int16 GetInt16(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取 32 位无符号整数值
     *
     * 将数据库原始数据转换为 uint32 类型（0-4294967295）。
     * 这是最常用的整数类型之一，用于存储游戏中的各种标识符和计数器。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的 32 位无符号整数值
     *
     * @note 如果源数据超出 uint32 范围，可能会发生截断并记录日志
     */
    virtual uint32 GetUInt32(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取 32 位有符号整数值
     *
     * 将数据库原始数据转换为 int32 类型（-2147483648 到 2147483647）。
     * 常用于存储可能为负数的数值，如坐标偏移、属性修正值等。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的 32 位有符号整数值
     *
     * @note 如果源数据超出 int32 范围，可能会发生截断并记录日志
     */
    virtual int32 GetInt32(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取 64 位无符号整数值
     *
     * 将数据库原始数据转换为 uint64 类型。
     * 常用于存储大数值，如玩家 GUID、金币数量、经验值等。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的 64 位无符号整数值
     *
     * @note 提供了最大的整数范围，通常不会发生截断
     */
    virtual uint64 GetUInt64(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取 64 位有符号整数值
     *
     * 将数据库原始数据转换为 int64 类型。
     * 用于存储需要极大范围的有符号数值。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的 64 位有符号整数值
     */
    virtual int64 GetInt64(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取单精度浮点数值
     *
     * 将数据库原始数据转换为 float 类型（32 位 IEEE 754）。
     * 常用于存储坐标位置、角度、速度等需要小数的游戏数据。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的单精度浮点数值
     *
     * @note 精度约为 6-7 位有效数字，超出精度范围可能丢失精度
     */
    virtual float GetFloat(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取双精度浮点数值
     *
     * 将数据库原始数据转换为 double 类型（64 位 IEEE 754）。
     * 用于需要高精度计算的场合，如复杂的数学运算。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的双精度浮点数值
     *
     * @note 精度约为 15-16 位有效数字
     */
    virtual double GetDouble(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取日期时间值
     *
     * 将数据库原始数据转换为系统时间点类型（SystemTimePoint）。
     * 用于存储和读取时间戳、创建时间、过期时间等时间相关数据。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 转换后的系统时间点对象
     *
     * @note 数据库中通常以 DATETIME、TIMESTAMP 或 DATE 类型存储
     * @see SystemTimePoint
     */
    virtual SystemTimePoint GetDate(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 从数据库结果中获取 C 风格字符串
     *
     * 将数据库原始数据转换为 C 风格字符串（以 null 结尾的字符数组）。
     * 用于读取文本数据，如名称、描述、消息内容等。
     *
     * @param data 指向数据库字段的原始二进制数据的指针
     * @param size 数据的字节大小
     * @param meta 字段元数据，包含字段类型、表名、字段名等信息
     * @return 指向字符串的常量指针
     *
     * @note 返回的指针指向数据库结果集内部的缓冲区，生命周期由结果集管理
     * @warning 不要在结果集销毁后继续使用返回的指针
     */
    virtual char const* GetCString(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const = 0;

    /**
     * @brief 记录数据截断警告日志
     *
     * 当从数据库读取的数据在转换为目标类型时发生截断（如将大整数转换为小整数），
     * 调用此方法记录警告日志，帮助开发者定位数据类型不匹配的问题。
     *
     * @param getter 调用的获取方法名称，如 "GetUInt8"、"GetInt32" 等
     * @param meta 字段元数据，包含表名、字段名、字段类型等信息
     *
     * @note 该方法为静态方法，可被所有转换器实例共享
     * @note 日志级别为 LOG_WARN，在发布版本中也会记录
     *
     * 典型调用示例：
     * @code
     * // 当检测到数据截断时
     * if (value > UINT8_MAX)
     * {
     *     LogTruncation("GetUInt8", meta);
     *     return static_cast<uint8>(value);
     * }
     * @endcode
     */
    static void LogTruncation(char const* getter, QueryResultFieldMetadata const* meta);
};

#endif // TRINITY_FIELD_VALUE_CONVERTER_H
