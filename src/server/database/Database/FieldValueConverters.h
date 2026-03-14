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
 * @file FieldValueConverters.h
 * @brief 数据库字段值转换器模块
 *
 * 本文件提供了一系列模板类和工具类，用于在数据库查询结果和应用程序数据类型之间进行转换。
 * 主要包含以下功能：
 * - 将字符串格式的数据库值转换为目标数据类型
 * - 将二进制格式的数据库值转换为目标数据类型
 * - 提供基础类型转换器的实现框架
 * - 处理字符串类型的特化转换器
 * - 提供未实现功能的占位转换器
 *
 * 这些转换器是数据库抽象层的核心组件，确保从不同数据库引擎返回的数据能够
 * 以统一的格式供上层应用使用。
 */

#ifndef TRINITY_FIELD_VALUE_CONVERTERS_H
#define TRINITY_FIELD_VALUE_CONVERTERS_H

#include "FieldValueConverter.h"
#include "StringConvert.h"

/**
 * @brief 字符串到数据库类型转换器
 *
 * 将数据库查询返回的字符串值转换为指定的数据库类型。
 * 该转换器处理以文本格式返回的数据库字段值（如 MySQL 的文本协议）。
 *
 * @tparam DatabaseType 目标数据库类型（如 uint32, int64, float 等）
 */
template<typename DatabaseType>
class FromStringToDatabaseTypeConverter
{
public:
    /**
     * @brief 将字符串数据转换为数据库类型值
     *
     * 解析字符串内容并将其转换为目标数据库类型。
     * 如果转换失败，返回默认值 0。
     *
     * @param data 指向字符串数据的指针
     * @param size 字符串数据的字节长度
     * @return DatabaseType 转换后的数据库类型值，失败时返回 0
     */
    static DatabaseType GetDatabaseValue(char const* data, uint32 size)
    {
        return Trinity::StringTo<DatabaseType>({ data, size }).template value_or<DatabaseType>(0);
    }

    /**
     * @brief 获取字符串值
     *
     * 对于字符串类型的数据，直接返回原始数据指针。
     *
     * @param data 指向字符串数据的指针
     * @return char const* 原始字符串指针
     */
    static char const* GetStringValue(char const* data)
    {
        return data;
    }
};

/**
 * @brief 二进制到数据库类型转换器
 *
 * 将数据库查询返回的二进制值转换为指定的数据库类型。
 * 该转换器处理以二进制格式返回的数据库字段值（如 MySQL 的二进制协议）。
 * 使用 reinterpret_cast 直接读取二进制数据，提供更高的性能。
 *
 * @tparam DatabaseType 目标数据库类型（如 uint32, int64, float 等）
 */
template<typename DatabaseType>
class FromBinaryToDatabaseTypeConverter
{
public:
    /**
     * @brief 将二进制数据转换为数据库类型值
     *
     * 通过 reinterpret_cast 直接将二进制数据解释为目标类型。
     * 要求二进制数据的内存布局与目标类型一致。
     *
     * @param data 指向二进制数据的指针
     * @param size 二进制数据的字节长度（未使用）
     * @return DatabaseType 转换后的数据库类型值
     */
    static DatabaseType GetDatabaseValue(char const* data, uint32 /*size*/)
    {
        return *reinterpret_cast<DatabaseType const*>(data);
    }

    /**
     * @brief 获取字符串值
     *
     * 二进制转换器不支持直接获取字符串值，始终返回 nullptr。
     *
     * @param data 指向数据的指针（未使用）
     * @return char const* 始终返回 nullptr
     */
    static char const* GetStringValue(char const* /*data*/)
    {
        return nullptr;
    }
};

/**
 * @brief 基础类型结果值转换器
 *
 * 将数据库查询结果从元数据指定的类型转换为 Field::Get* 函数请求的类型。
 * 该类是所有基础数据类型转换器的基类，提供类型安全的数值转换和溢出检测。
 *
 * 当请求的类型与数据库字段的原始类型不匹配时，会进行类型转换并检测是否发生截断。
 * 如果检测到数据截断，会记录警告日志并返回默认值。
 *
 * @tparam DatabaseType 数据库字段存储的数据类型
 * @tparam ToDatabaseTypeConverter 用于将原始数据转换为数据库类型的转换器模板
 */
template<typename DatabaseType, template<typename> typename ToDatabaseTypeConverter>
class PrimitiveResultValueConverter : public BaseDatabaseResultValueConverter
{
public:
    /**
     * @brief 获取数值类型字段值
     *
     * 将数据库字段值转换为指定的数值类型，并检测是否存在数据截断。
     * 如果转换后的值无法完整还原回原类型，说明发生了数据截断，
     * 此时记录警告日志并返回类型 T 的默认值。
     *
     * @tparam T 请求的目标数值类型（如 uint8, int32, float 等）
     * @param data 指向原始字段数据的指针
     * @param size 原始字段数据的字节长度
     * @param meta 字段元数据信息，包含字段名、表名等
     * @param func 调用该转换的函数名，用于日志记录
     * @return T 转换后的数值，如发生截断则返回 T()
     */
    template<typename T>
    static T GetNumericValue(char const* data, uint32 size, QueryResultFieldMetadata const* meta, char const* func)
    {
        DatabaseType source = ToDatabaseTypeConverter<DatabaseType>::GetDatabaseValue(data, size);
        T result = static_cast<T>(source);
        if (static_cast<DatabaseType>(result) != source)
        {
            LogTruncation(func, meta);
            return T();
        }
        return result;
    }

    /**
     * @brief 获取 8 位无符号整数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return uint8 转换后的 8 位无符号整数
     */
    uint8 GetUInt8(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<uint8>(data, size, meta, "Field::GetUInt8"); }

    /**
     * @brief 获取 8 位有符号整数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return int8 转换后的 8 位有符号整数
     */
    int8 GetInt8(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<int8>(data, size, meta, "Field::GetInt8"); }

    /**
     * @brief 获取 16 位无符号整数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return uint16 转换后的 16 位无符号整数
     */
    uint16 GetUInt16(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<uint16>(data, size, meta, "Field::GetUInt16"); }

    /**
     * @brief 获取 16 位有符号整数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return int16 转换后的 16 位有符号整数
     */
    int16 GetInt16(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<int16>(data, size, meta, "Field::GetInt16"); }

    /**
     * @brief 获取 32 位无符号整数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return uint32 转换后的 32 位无符号整数
     */
    uint32 GetUInt32(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<uint32>(data, size, meta, "Field::GetUInt32"); }

    /**
     * @brief 获取 32 位有符号整数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return int32 转换后的 32 位有符号整数
     */
    int32 GetInt32(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<int32>(data, size, meta, "Field::GetInt32"); }

    /**
     * @brief 获取 64 位无符号整数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return uint64 转换后的 64 位无符号整数
     */
    uint64 GetUInt64(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<uint64>(data, size, meta, "Field::GetUInt64"); }

    /**
     * @brief 获取 64 位有符号整数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return int64 转换后的 64 位有符号整数
     */
    int64 GetInt64(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<int64>(data, size, meta, "Field::GetInt64"); }

    /**
     * @brief 获取单精度浮点数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return float 转换后的单精度浮点数
     */
    float GetFloat(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<float>(data, size, meta, "Field::GetFloat"); }

    /**
     * @brief 获取双精度浮点数值
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return double 转换后的双精度浮点数
     */
    double GetDouble(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override { return GetNumericValue<double>(data, size, meta, "Field::GetDouble"); }

    /**
     * @brief 获取日期时间值
     *
     * 基础类型转换器不支持日期时间类型，记录截断警告并返回最小时间点。
     *
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return SystemTimePoint 返回 SystemTimePoint::min()
     */
    SystemTimePoint GetDate(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetDate", meta); return SystemTimePoint::min(); }

    /**
     * @brief 获取 C 风格字符串指针
     *
     * 尝试从数据中获取字符串值。如果数据存在但转换器无法提供字符串表示，
     * 则记录截断警告。
     *
     * @param data 原始数据指针
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return char const* 字符串指针，如无法转换则可能返回 nullptr
     */
    char const* GetCString(char const* data, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override
    {
        char const* result = ToDatabaseTypeConverter<DatabaseType>::GetStringValue(data);
        if (data && !result)
            LogTruncation("Field::GetCString", meta);
        return result;
    }
};

/**
 * @brief 字符串类型结果值转换器的特化版本
 *
 * 这是 PrimitiveResultValueConverter 针对字符串类型（char const*）的特化实现。
 * 字符串字段只能通过 GetCString 获取，尝试调用其他 Get* 方法会记录截断警告。
 *
 * 该特化版本用于处理数据库中的 VARCHAR、TEXT 等字符串类型字段。
 */
template<>
class PrimitiveResultValueConverter<char const*, std::type_identity_t> : public BaseDatabaseResultValueConverter
{
public:
    /**
     * @brief 获取 8 位无符号整数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return uint8 始终返回 0
     */
    uint8 GetUInt8(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt8", meta); return 0; }

    /**
     * @brief 获取 8 位有符号整数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return int8 始终返回 0
     */
    int8 GetInt8(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt8", meta); return 0; }

    /**
     * @brief 获取 16 位无符号整数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return uint16 始终返回 0
     */
    uint16 GetUInt16(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt16", meta); return 0; }

    /**
     * @brief 获取 16 位有符号整数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return int16 始终返回 0
     */
    int16 GetInt16(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt16", meta); return 0; }

    /**
     * @brief 获取 32 位无符号整数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return uint32 始终返回 0
     */
    uint32 GetUInt32(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt32", meta); return 0; }

    /**
     * @brief 获取 32 位有符号整数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return int32 始终返回 0
     */
    int32 GetInt32(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt32", meta); return 0; }

    /**
     * @brief 获取 64 位无符号整数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return uint64 始终返回 0
     */
    uint64 GetUInt64(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt64", meta); return 0; }

    /**
     * @brief 获取 64 位有符号整数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return int64 始终返回 0
     */
    int64 GetInt64(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt64", meta); return 0; }

    /**
     * @brief 获取单精度浮点数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return float 始终返回 0.0f
     */
    float GetFloat(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetFloat", meta); return 0.0f; }

    /**
     * @brief 获取双精度浮点数值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return double 始终返回 0.0
     */
    double GetDouble(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetDouble", meta); return 0.0; }

    /**
     * @brief 获取日期时间值（字符串类型不支持）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return SystemTimePoint 始终返回 SystemTimePoint::min()
     */
    SystemTimePoint GetDate(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetDate", meta); return SystemTimePoint::min(); }

    /**
     * @brief 获取 C 风格字符串指针
     *
     * 这是字符串类型转换器唯一支持的获取方法，直接返回原始字符串指针。
     *
     * @param data 原始数据指针
     * @param size 数据大小（未使用）
     * @param meta 字段元数据（未使用）
     * @return char const* 原始字符串指针
     */
    char const* GetCString(char const* data, uint32 /*size*/, QueryResultFieldMetadata const* /*meta*/) const override { return data; }
};

/**
 * @brief 字符串结果值转换器类型别名
 *
 * 简化 PrimitiveResultValueConverter<char const*, std::type_identity_t> 的使用，
 * 专门用于处理数据库中的字符串类型字段。
 */
using StringResultValueConverter = PrimitiveResultValueConverter<char const*, std::type_identity_t>;

/**
 * @brief 未实现的结果值转换器
 *
 * 这是一个占位转换器，用于处理尚未实现的数据库字段类型。
 * 所有 Get* 方法都会记录截断警告并返回默认值，表示该类型转换尚未实现。
 *
 * 当添加新的数据库字段类型支持时，应该创建专门的转换器类替代此占位实现。
 */
class NotImplementedResultValueConverter : public BaseDatabaseResultValueConverter
{
public:
    /**
     * @brief 获取 8 位无符号整数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return uint8 始终返回 0
     */
    uint8 GetUInt8(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt8", meta); return 0; }

    /**
     * @brief 获取 8 位有符号整数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return int8 始终返回 0
     */
    int8 GetInt8(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt8", meta); return 0; }

    /**
     * @brief 获取 16 位无符号整数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return uint16 始终返回 0
     */
    uint16 GetUInt16(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt16", meta); return 0; }

    /**
     * @brief 获取 16 位有符号整数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return int16 始终返回 0
     */
    int16 GetInt16(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt16", meta); return 0; }

    /**
     * @brief 获取 32 位无符号整数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return uint32 始终返回 0
     */
    uint32 GetUInt32(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt32", meta); return 0; }

    /**
     * @brief 获取 32 位有符号整数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return int32 始终返回 0
     */
    int32 GetInt32(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt32", meta); return 0; }

    /**
     * @brief 获取 64 位无符号整数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return uint64 始终返回 0
     */
    uint64 GetUInt64(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt64", meta); return 0; }

    /**
     * @brief 获取 64 位有符号整数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return int64 始终返回 0
     */
    int64 GetInt64(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt64", meta); return 0; }

    /**
     * @brief 获取单精度浮点数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return float 始终返回 0.0f
     */
    float GetFloat(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetFloat", meta); return 0.0f; }

    /**
     * @brief 获取双精度浮点数值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return double 始终返回 0.0
     */
    double GetDouble(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetDouble", meta); return 0.0; }

    /**
     * @brief 获取日期时间值（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return SystemTimePoint 始终返回 SystemTimePoint::min()
     */
    SystemTimePoint GetDate(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetDate", meta); return SystemTimePoint::min(); }

    /**
     * @brief 获取 C 风格字符串指针（未实现）
     * @param data 原始数据指针（未使用）
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return char const* 始终返回 nullptr
     */
    char const* GetCString(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetCString", meta); return nullptr; }
};

#endif // TRINITY_FIELD_VALUE_CONVERTERS_H
