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
 * @file QueryResult.cpp
 * @brief SQL查询结果集实现文件
 *
 * 本文件实现了ResultSet和PreparedResultSet类，提供了MySQL查询结果的封装。
 * 主要功能包括：
 * - MySQL原生结果集的包装和资源管理
 * - 字段类型转换和元数据管理
 * - 数据行遍历和字段访问
 * - MySQL C API数据类型到C++类型的映射
 */

#include "QueryResult.h"
#include "Errors.h"
#include "Field.h"
#include "FieldValueConverters.h"
#include "Log.h"
#include "MySQLHacks.h"
#include "MySQLWorkaround.h"
#include <chrono>
#include <cstring>

/**
 * @brief 匿名命名空间，包含内部辅助函数
 *
 * 这些函数仅在当前编译单元内可见，用于内部实现细节
 */
namespace
{
/**
 * @brief 根据MySQL字段类型获取数据大小
 *
 * @param field MySQL字段元数据指针
 * @return uint32 数据大小（字节数）
 *
 * 该函数用于预处理结果集初始化时，计算每个字段的缓冲区大小。
 * 不同的MySQL数据类型有不同的存储大小。
 *
 * 调用时机：PreparedResultSet构造函数中调用
 * 性能注意事项：简单的switch语句，O(1)时间复杂度
 *
 * 返回值说明：
 * - NULL类型：0字节
 * - TINYINT：1字节
 * - SMALLINT/YEAR：2字节
 * - INT24/INT/FLOAT：4字节
 * - BIGINT/DOUBLE/BIT：8字节
 * - 日期时间类型：sizeof(MYSQL_TIME)
 * - 字符串/BLOB：max_length + 1（包含null终止符）
 * - DECIMAL：64字节
 */
static uint32 SizeForType(MYSQL_FIELD* field)
{
    switch (field->type)
    {
        case MYSQL_TYPE_NULL:
            return 0;
        case MYSQL_TYPE_TINY:
            return 1;
        case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_SHORT:
            return 2;
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_FLOAT:
            return 4;
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_BIT:
            return 8;

        case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_DATETIME:
            return sizeof(MYSQL_TIME);

        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VAR_STRING:
            // 字符串和BLOB类型，需要额外的null终止符
            return field->max_length + 1;

        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
            return 64;

        case MYSQL_TYPE_GEOMETRY:
            /*
            以下类型不会通过网络传输：
            MYSQL_TYPE_ENUM:
            MYSQL_TYPE_SET:
            */
        default:
            TC_LOG_WARN("sql.sql", "SQL::SizeForType(): invalid field type {}", uint32(field->type));
            return 0;
    }
}

/**
 * @brief 将MySQL字段类型转换为TrinityCore数据库字段类型
 *
 * @param type MySQL字段类型枚举
 * @param flags MySQL字段标志（如UNSIGNED_FLAG）
 * @return DatabaseFieldTypes TrinityCore字段类型枚举
 *
 * 该函数将MySQL的原生数据类型映射到TrinityCore统一的字段类型系统。
 * 对于有符号/无符号的类型，通过flags参数区分。
 *
 * 调用时机：初始化字段元数据时调用
 * 性能注意事项：简单的switch语句，O(1)时间复杂度
 *
 * 类型映射规则：
 * - MYSQL_TYPE_NULL -> Null
 * - MYSQL_TYPE_TINY -> Int8或UInt8（根据UNSIGNED_FLAG）
 * - MYSQL_TYPE_SHORT/YEAR -> Int16或UInt16
 * - MYSQL_TYPE_LONG/INT24 -> Int32或UInt32
 * - MYSQL_TYPE_LONGLONG/BIT -> Int64或UInt64
 * - MYSQL_TYPE_FLOAT -> Float
 * - MYSQL_TYPE_DOUBLE -> Double
 * - MYSQL_TYPE_DECIMAL -> Decimal
 * - MYSQL_TYPE_DATE/DATETIME/TIMESTAMP -> Date
 * - MYSQL_TYPE_TIME -> Time
 * - 字符串/BLOB -> Binary
 */
DatabaseFieldTypes MysqlTypeToFieldType(enum_field_types type, uint32 flags)
{
    switch (type)
    {
        case MYSQL_TYPE_NULL:
            return DatabaseFieldTypes::Null;
        case MYSQL_TYPE_TINY:
            // 根据UNSIGNED标志判断是有符号还是无符号
            return (flags & UNSIGNED_FLAG) ? DatabaseFieldTypes::UInt8 : DatabaseFieldTypes::Int8;
        case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_SHORT:
            return (flags & UNSIGNED_FLAG) ? DatabaseFieldTypes::UInt16 : DatabaseFieldTypes::Int16;
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONG:
            return (flags & UNSIGNED_FLAG) ? DatabaseFieldTypes::UInt32 : DatabaseFieldTypes::Int32;
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_BIT:
            return (flags & UNSIGNED_FLAG) ? DatabaseFieldTypes::UInt64 : DatabaseFieldTypes::Int64;
        case MYSQL_TYPE_FLOAT:
            return DatabaseFieldTypes::Float;
        case MYSQL_TYPE_DOUBLE:
            return DatabaseFieldTypes::Double;
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
            return DatabaseFieldTypes::Decimal;
        case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_DATETIME:
            return DatabaseFieldTypes::Date;
        case MYSQL_TYPE_TIME:
            return DatabaseFieldTypes::Time;
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VAR_STRING:
            return DatabaseFieldTypes::Binary;
        default:
            TC_LOG_WARN("sql.sql", "MysqlTypeToFieldType(): invalid field type {}", uint32(type));
            break;
    }

    return DatabaseFieldTypes::Null;
}

/**
 * @brief 将MySQL字段类型转换为可读字符串
 *
 * @param type MySQL字段类型枚举
 * @param flags MySQL字段标志
 * @return char const* 类型名称字符串
 *
 * 该函数用于日志和调试，将MySQL字段类型转换为人类可读的字符串表示。
 *
 * 调用时机：记录日志或调试输出时调用
 * 性能注意事项：简单的switch语句，O(1)时间复杂度
 */
static char const* FieldTypeToString(enum_field_types type, uint32 flags)
{
    switch (type)
    {
        case MYSQL_TYPE_BIT:         return "BIT";
        case MYSQL_TYPE_BLOB:        return "BLOB";
        case MYSQL_TYPE_DATE:        return "DATE";
        case MYSQL_TYPE_DATETIME:    return "DATETIME";
        case MYSQL_TYPE_NEWDECIMAL:  return "NEWDECIMAL";
        case MYSQL_TYPE_DECIMAL:     return "DECIMAL";
        case MYSQL_TYPE_DOUBLE:      return "DOUBLE";
        case MYSQL_TYPE_ENUM:        return "ENUM";
        case MYSQL_TYPE_FLOAT:       return "FLOAT";
        case MYSQL_TYPE_GEOMETRY:    return "GEOMETRY";
        case MYSQL_TYPE_INT24:       return (flags & UNSIGNED_FLAG) ? "UNSIGNED INT24" : "INT24";
        case MYSQL_TYPE_LONG:        return (flags & UNSIGNED_FLAG) ? "UNSIGNED LONG" : "LONG";
        case MYSQL_TYPE_LONGLONG:    return (flags & UNSIGNED_FLAG) ? "UNSIGNED LONGLONG" : "LONGLONG";
        case MYSQL_TYPE_LONG_BLOB:   return "LONG_BLOB";
        case MYSQL_TYPE_MEDIUM_BLOB: return "MEDIUM_BLOB";
        case MYSQL_TYPE_NEWDATE:     return "NEWDATE";
        case MYSQL_TYPE_NULL:        return "NULL";
        case MYSQL_TYPE_SET:         return "SET";
        case MYSQL_TYPE_SHORT:       return (flags & UNSIGNED_FLAG) ? "UNSIGNED SHORT" : "SHORT";
        case MYSQL_TYPE_STRING:      return "STRING";
        case MYSQL_TYPE_TIME:        return "TIME";
        case MYSQL_TYPE_TIMESTAMP:   return "TIMESTAMP";
        case MYSQL_TYPE_TINY:        return (flags & UNSIGNED_FLAG) ? "UNSIGNED TINY" : "TINY";
        case MYSQL_TYPE_TINY_BLOB:   return "TINY_BLOB";
        case MYSQL_TYPE_VAR_STRING:  return "VAR_STRING";
        case MYSQL_TYPE_YEAR:        return "YEAR";
        default:                     return "-Unknown-";
    }
}

/**
 * @class FromStringToMYSQL_TIME
 * @brief 字符串到MySQL时间类型的转换器模板
 *
 * 该模板类用于将字符串格式的日期时间数据转换为MySQL的MYSQL_TIME结构。
 * 支持多种格式：
 * - 时间格式：HH:MM:SS
 * - 日期格式：YYYY-MM-DD
 * - 日期时间格式：YYYY-MM-DD HH:MM:SS[.microseconds]
 *
 * 调用时机：在从文本协议结果集中读取日期时间字段时使用
 * 性能注意事项：字符串解析，开销与字符串长度成正比
 */
template <typename>
class FromStringToMYSQL_TIME
{
public:
    /**
     * @brief 将字符串转换为MYSQL_TIME结构
     *
     * @param data 字符串数据指针
     * @param size 字符串长度
     * @return MYSQL_TIME MySQL时间结构
     *
     * 该方法解析字符串并填充MYSQL_TIME结构的各个字段。
     * 根据分隔符自动判断是时间、日期还是日期时间格式。
     */
    static MYSQL_TIME GetDatabaseValue(char const* data, uint32 size)
    {
        MYSQL_TIME result = {};
        // 空数据返回NONE类型
        if (!data || !size)
        {
            result.time_type = MYSQL_TIMESTAMP_NONE;
            return result;
        }

        std::string_view in(data, size);

        // 查找第一个分隔符，用于判断格式类型
        size_t firstSeparatorIndex = in.find_first_of(":-");
        if (firstSeparatorIndex == std::string_view::npos)
        {
            result.time_type = MYSQL_TIMESTAMP_NONE;
            return result;
        }

        char firstSeparator = in[firstSeparatorIndex];

        // Lambda函数：解析下一个数值组件
        auto parseNextComponent = [&](uint32& value, char requiredSeparator = '\0') -> bool
        {
            std::from_chars_result parseResult = std::from_chars(in.data(), in.data() + in.size(), value);
            if (parseResult.ec != std::errc())
                return false;

            in.remove_prefix(parseResult.ptr - in.data());
            if (requiredSeparator)
            {
                if (in.empty() || in[0] != requiredSeparator)
                    return false;

                in.remove_prefix(1);
            }

            return true;
        };

        // 解析前三个组件（年/小时、月/分钟、日/秒）
        uint32 yearOrHours = 0;
        uint32 monthOrMinutes = 0;
        uint32 dayOrSeconds = 0;
        if (!parseNextComponent(yearOrHours, firstSeparator)
            || !parseNextComponent(monthOrMinutes, firstSeparator)
            || !parseNextComponent(dayOrSeconds))
        {
            result.time_type = MYSQL_TIMESTAMP_ERROR;
            return result;
        }

        // 根据分隔符判断是时间还是日期格式
        if (firstSeparator == ':')
        {
            // 时间格式：HH:MM:SS
            if (!in.empty())
            {
                result.time_type = MYSQL_TIMESTAMP_ERROR;
                return result;
            }

            // 填充时间字段
            result.hour = yearOrHours;
            result.minute = monthOrMinutes;
            result.second = dayOrSeconds;
            result.time_type = MYSQL_TIMESTAMP_TIME;
        }
        else
        {
            // 日期或日期时间格式
            if (in.empty())
            {
                // 纯日期格式：YYYY-MM-DD
                result.year = yearOrHours;
                result.month = monthOrMinutes;
                result.day = dayOrSeconds;
                result.time_type = MYSQL_TIMESTAMP_DATE;
                return result;
            }

            // 日期时间格式：YYYY-MM-DD HH:MM:SS[.microseconds]
            if (in[0] != ' ')
            {
                result.time_type = MYSQL_TIMESTAMP_ERROR;
                return result;
            }

            in.remove_prefix(1);

            // 解析时间部分
            uint32 hours = 0;
            uint32 minutes = 0;
            uint32 seconds = 0;
            if (!parseNextComponent(hours, ':')
                || !parseNextComponent(minutes, ':')
                || !parseNextComponent(seconds))
            {
                result.time_type = MYSQL_TIMESTAMP_ERROR;
                return result;
            }

            // 解析微秒部分（可选）
            uint32 microseconds = 0;
            if (!in.empty())
            {
                if (in[0] != '.')
                {
                    result.time_type = MYSQL_TIMESTAMP_ERROR;
                    return result;
                }

                in.remove_prefix(1);
                if (!parseNextComponent(microseconds))
                {
                    result.time_type = MYSQL_TIMESTAMP_ERROR;
                    return result;
                }

                if (!in.empty())
                {
                    result.time_type = MYSQL_TIMESTAMP_ERROR;
                    return result;
                }
            }

            // 填充完整的日期时间字段
            result.year = yearOrHours;
            result.month = monthOrMinutes;
            result.day = dayOrSeconds;
            result.hour = hours;
            result.minute = minutes;
            result.second = seconds;
            result.second_part = microseconds;
            result.time_type = MYSQL_TIMESTAMP_DATETIME;
        }

        return result;
    }

    /**
     * @brief 获取字符串值
     *
     * @param data 原始字符串数据
     * @return char const* 原样返回字符串指针
     */
    static char const* GetStringValue(char const* data)
    {
        return data;
    }
};

/**
 * @class DateResultValueConverter
 * @brief 日期时间字段值转换器
 *
 * 该模板类用于将MySQL日期时间数据转换为C++类型。
 * 主要支持转换为SystemTimePoint类型和字符串类型。
 *
 * @tparam ToDatabaseTypeConverter 用于将原始数据转换为MYSQL_TIME的转换器模板
 *
 * 调用时机：读取日期时间类型的字段值时使用
 * 性能注意事项：包含时间解析和转换，开销中等
 */
template<template<typename> typename ToDatabaseTypeConverter>
class DateResultValueConverter : public BaseDatabaseResultValueConverter
{
    // 数值类型的转换方法，记录截断警告
    uint8 GetUInt8(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt8", meta); return 0; }
    int8 GetInt8(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt8", meta); return 0; }
    uint16 GetUInt16(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt16", meta); return 0; }
    int16 GetInt16(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt16", meta); return 0; }
    uint32 GetUInt32(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt32", meta); return 0; }
    int32 GetInt32(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt32", meta); return 0; }
    uint64 GetUInt64(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetUInt64", meta); return 0; }
    int64 GetInt64(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetInt64", meta); return 0; }
    float GetFloat(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetFloat", meta); return 0.0f; }
    double GetDouble(char const* /*data*/, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override { LogTruncation("Field::GetDouble", meta); return 0.0; }

    /**
     * @brief 将日期时间数据转换为SystemTimePoint
     *
     * @param data 原始数据指针
     * @param size 数据大小
     * @param meta 字段元数据
     * @return SystemTimePoint C++时间点对象
     *
     * 支持DATE和DATETIME两种MySQL类型，将其转换为C++的system_clock时间点。
     */
    SystemTimePoint GetDate(char const* data, uint32 size, QueryResultFieldMetadata const* meta) const override
    {
        using namespace std::chrono;
        // 将原始数据转换为MYSQL_TIME结构
        MYSQL_TIME source = ToDatabaseTypeConverter<MYSQL_TIME>::GetDatabaseValue(data, size);
        switch (source.time_type)
        {
            case MYSQL_TIMESTAMP_DATE:
                // 纯日期格式：转换为当天的起始时间点
                return sys_days(year(source.year) / month(source.month) / day(source.day));
            case MYSQL_TIMESTAMP_DATETIME:
                // 日期时间格式：转换为完整的时间点
                return sys_days(year(source.year) / month(source.month) / day(source.day))
                    + hours(source.hour)
                    + minutes(source.minute)
                    + seconds(source.second)
                    + microseconds(source.second_part);
            default:
                break;
        }

        // 无效类型，记录警告并返回默认值
        LogTruncation("Field::GetDate", meta);
        return SystemTimePoint();
    }

    /**
     * @brief 获取日期时间的字符串表示
     *
     * @param data 原始数据指针
     * @param size 数据大小（未使用）
     * @param meta 字段元数据
     * @return char const* 字符串指针
     */
    char const* GetCString(char const* data, uint32 /*size*/, QueryResultFieldMetadata const* meta) const override
    {
        char const* result = ToDatabaseTypeConverter<MYSQL_TIME>::GetStringValue(data);
        if (data && !result)
            LogTruncation("Field::GetCString", meta);
        return result;
    }
};

/**
 * @brief 字符串协议字段值转换器数组
 *
 * 该数组为每种数据库字段类型提供对应的转换器，用于文本协议结果集。
 * 索引对应DatabaseFieldTypes枚举值。
 */
std::unique_ptr<BaseDatabaseResultValueConverter> const FromStringValueConverters[15] =
{
    nullptr,                                                                                    // Null
    std::make_unique<PrimitiveResultValueConverter<uint8, FromStringToDatabaseTypeConverter>>(),   // UInt8
    std::make_unique<PrimitiveResultValueConverter<int8, FromStringToDatabaseTypeConverter>>(),    // Int8
    std::make_unique<PrimitiveResultValueConverter<uint16, FromStringToDatabaseTypeConverter>>(),  // UInt16
    std::make_unique<PrimitiveResultValueConverter<int16, FromStringToDatabaseTypeConverter>>(),   // Int16
    std::make_unique<PrimitiveResultValueConverter<uint32, FromStringToDatabaseTypeConverter>>(),  // UInt32
    std::make_unique<PrimitiveResultValueConverter<int32, FromStringToDatabaseTypeConverter>>(),   // Int32
    std::make_unique<PrimitiveResultValueConverter<uint64, FromStringToDatabaseTypeConverter>>(),  // UInt64
    std::make_unique<PrimitiveResultValueConverter<int64, FromStringToDatabaseTypeConverter>>(),   // Int64
    std::make_unique<PrimitiveResultValueConverter<float, FromStringToDatabaseTypeConverter>>(),    // Float
    std::make_unique<PrimitiveResultValueConverter<double, FromStringToDatabaseTypeConverter>>(),   // Double
    std::make_unique<PrimitiveResultValueConverter<double, FromStringToDatabaseTypeConverter>>(),   // Decimal（作为double处理）
    std::make_unique<DateResultValueConverter<FromStringToMYSQL_TIME>>(),                            // Date
    std::make_unique<NotImplementedResultValueConverter>(),    // DatabaseFieldTypes::Time
    std::make_unique<StringResultValueConverter>()             // Binary
};

/**
 * @brief 二进制协议字段值转换器数组
 *
 * 该数组为每种数据库字段类型提供对应的转换器，用于预处理语句结果集。
 * 索引对应DatabaseFieldTypes枚举值。
 */
std::unique_ptr<BaseDatabaseResultValueConverter> const BinaryValueConverters[15] =
{
    nullptr,                                                                                     // Null
    std::make_unique<PrimitiveResultValueConverter<uint8, FromBinaryToDatabaseTypeConverter>>(),    // UInt8
    std::make_unique<PrimitiveResultValueConverter<int8, FromBinaryToDatabaseTypeConverter>>(),     // Int8
    std::make_unique<PrimitiveResultValueConverter<uint16, FromBinaryToDatabaseTypeConverter>>(),   // UInt16
    std::make_unique<PrimitiveResultValueConverter<int16, FromBinaryToDatabaseTypeConverter>>(),    // Int16
    std::make_unique<PrimitiveResultValueConverter<uint32, FromBinaryToDatabaseTypeConverter>>(),   // UInt32
    std::make_unique<PrimitiveResultValueConverter<int32, FromBinaryToDatabaseTypeConverter>>(),    // Int32
    std::make_unique<PrimitiveResultValueConverter<uint64, FromBinaryToDatabaseTypeConverter>>(),   // UInt64
    std::make_unique<PrimitiveResultValueConverter<int64, FromBinaryToDatabaseTypeConverter>>(),    // Int64
    std::make_unique<PrimitiveResultValueConverter<float, FromBinaryToDatabaseTypeConverter>>(),     // Float
    std::make_unique<PrimitiveResultValueConverter<double, FromBinaryToDatabaseTypeConverter>>(),    // Double
    std::make_unique<PrimitiveResultValueConverter<double, FromStringToDatabaseTypeConverter>>(),    // Decimal（总是以字符串发送）
    std::make_unique<DateResultValueConverter<FromBinaryToDatabaseTypeConverter>>(),                 // Date
    std::make_unique<NotImplementedResultValueConverter>(),    // DatabaseFieldTypes::Time
    std::make_unique<StringResultValueConverter>()             // Binary
};

/**
 * @brief 初始化数据库字段元数据
 *
 * @param meta 要初始化的元数据对象指针
 * @param field MySQL字段信息
 * @param fieldIndex 字段索引
 * @param binaryProtocol 是否使用二进制协议
 *
 * 该函数填充QueryResultFieldMetadata结构，包括表名、字段名、类型等信息，
 * 并根据协议类型选择合适的值转换器。
 *
 * 调用时机：结果集构造时，为每个字段调用一次
 * 性能注意事项：简单的字符串拷贝和类型转换，开销小
 */
void InitializeDatabaseFieldMetadata(QueryResultFieldMetadata* meta, MySQLField const* field, uint32 fieldIndex, bool binaryProtocol)
{
    meta->TableName = field->org_table;   // 原始表名
    meta->TableAlias = field->table;       // 表别名
    meta->Name = field->org_name;          // 原始字段名
    meta->Alias = field->name;             // 字段别名
    meta->TypeName = FieldTypeToString(field->type, field->flags);  // 类型名称字符串
    meta->Index = fieldIndex;              // 字段索引
    meta->Type = MysqlTypeToFieldType(field->type, field->flags);   // 字段类型枚举
    // 根据协议类型选择转换器
    meta->Converter = binaryProtocol ? BinaryValueConverters[AsUnderlyingType(meta->Type)].get() : FromStringValueConverters[AsUnderlyingType(meta->Type)].get();
}
} // end anonymous namespace

/**
 * @brief ResultSet构造函数
 *
 * @param result MySQL结果集对象指针
 * @param fields MySQL字段元数据数组指针
 * @param rowCount 结果行数
 * @param fieldCount 字段数量
 *
 * 初始化ResultSet对象，分配Field数组并设置元数据。
 * 使用文本协议，数据将在NextRow()调用时逐行加载。
 *
 * 初始化流程：
 * 1. 保存基本参数
 * 2. 分配字段元数据向量
 * 3. 分配Field数组
 * 4. 为每个字段初始化元数据和Field对象
 */
ResultSet::ResultSet(MySQLResult* result, MySQLField* fields, uint64 rowCount, uint32 fieldCount) :
_rowCount(rowCount),
_fieldCount(fieldCount),
_result(result),
_fields(fields)
{
    // 分配字段元数据存储空间
    _fieldMetadata.resize(_fieldCount);
    // 分配当前行Field数组
    _currentRow = new Field[_fieldCount];
    // 为每个字段初始化元数据和Field对象
    for (uint32 i = 0; i < _fieldCount; i++)
    {
        // 初始化字段元数据（使用文本协议）
        InitializeDatabaseFieldMetadata(&_fieldMetadata[i], &_fields[i], i, false);
        // 为Field对象设置元数据引用
        _currentRow[i].SetMetadata(&_fieldMetadata[i]);
    }
}

/**
 * @brief PreparedResultSet构造函数
 *
 * @param stmt MySQL预处理语句对象指针
 * @param result MySQL结果元数据对象指针
 * @param rowCount 预估行数
 * @param fieldCount 字段数量
 *
 * 初始化PreparedResultSet对象，一次性加载所有结果数据到内存。
 * 使用二进制协议，数据在构造时全部加载。
 *
 * 初始化流程：
 * 1. 检查元数据有效性
 * 2. 分配绑定结构
 * 3. 调用mysql_stmt_store_result加载所有数据
 * 4. 设置绑定缓冲区
 * 5. 遍历所有行，将数据存入m_rows向量
 * 6. 清理MySQL C API结构
 *
 * 性能注意事项：
 * - 会一次性加载所有数据到内存
 * - 内存占用 = 行数 * 每行大小
 * - 对于大结果集可能导致内存压力
 */
PreparedResultSet::PreparedResultSet(MySQLStmt* stmt, MySQLResult* result, uint64 rowCount, uint32 fieldCount) :
m_rowCount(rowCount),
m_rowPosition(0),
m_fieldCount(fieldCount),
m_rBind(nullptr),
m_stmt(stmt),
m_metadataResult(result)
{
    // 检查元数据是否有效
    if (!m_metadataResult)
        return;

    // 如果之前已经绑定过结果，先清理旧的绑定
    if (m_stmt->bind_result_done)
    {
        delete[] m_stmt->bind->length;
        delete[] m_stmt->bind->is_null;
    }

    // 分配绑定结构数组
    m_rBind = new MySQLBind[m_fieldCount];

    // 分配辅助数组（用于MySQL C API）
    // 注意：这些指针会被mysql_stmt_bind_result移动到m_stmt->bind中
    // MYSQL_STMT的生命周期等于连接的生命周期
    MySQLBool* m_isNull = new MySQLBool[m_fieldCount];
    unsigned long* m_length = new unsigned long[m_fieldCount];

    // 初始化绑定结构
    memset(m_isNull, 0, sizeof(MySQLBool) * m_fieldCount);
    memset(m_rBind, 0, sizeof(MySQLBind) * m_fieldCount);
    memset(m_length, 0, sizeof(unsigned long) * m_fieldCount);

    // 存储整个结果集到客户端
    if (mysql_stmt_store_result(m_stmt))
    {
        TC_LOG_WARN("sql.sql", "{}:mysql_stmt_store_result, cannot bind result from MySQL server. Error: {}", __FUNCTION__, mysql_stmt_error(m_stmt));
        delete[] m_rBind;
        delete[] m_isNull;
        delete[] m_length;
        return;
    }

    // 获取实际行数（可能比预估的更准确）
    m_rowCount = mysql_stmt_num_rows(m_stmt);

    // 根据元数据准备缓冲区
    MySQLField* field = reinterpret_cast<MySQLField*>(mysql_fetch_fields(m_metadataResult));
    m_fieldMetadata.resize(m_fieldCount);
    std::size_t rowSize = 0;  // 单行数据总大小

    // 设置每个字段的绑定信息
    for (uint32 i = 0; i < m_fieldCount; ++i)
    {
        // 计算字段大小
        uint32 size = SizeForType(&field[i]);
        rowSize += size;

        // 初始化字段元数据（使用二进制协议）
        InitializeDatabaseFieldMetadata(&m_fieldMetadata[i], &field[i], i, true);

        // 设置绑定参数
        m_rBind[i].buffer_type = field[i].type;         // 数据类型
        m_rBind[i].buffer_length = size;                // 缓冲区大小
        m_rBind[i].length = &m_length[i];               // 实际长度指针
        m_rBind[i].is_null = &m_isNull[i];              // NULL标志指针
        m_rBind[i].error = nullptr;                     // 错误标志
        m_rBind[i].is_unsigned = field[i].flags & UNSIGNED_FLAG;  // 无符号标志
    }

    // 分配数据缓冲区（所有行的所有字段）
    char* dataBuffer = new char[rowSize * m_rowCount];
    // 设置每个字段的缓冲区指针
    for (uint32 i = 0, offset = 0; i < m_fieldCount; ++i)
    {
        m_rBind[i].buffer = dataBuffer + offset;
        offset += m_rBind[i].buffer_length;
    }

    // 将绑定结构绑定到语句
    if (mysql_stmt_bind_result(m_stmt, m_rBind))
    {
        TC_LOG_WARN("sql.sql", "{}:mysql_stmt_bind_result, cannot bind result from MySQL server. Error: {}", __FUNCTION__, mysql_stmt_error(m_stmt));
        mysql_stmt_free_result(m_stmt);
        CleanUp();
        delete[] m_isNull;
        delete[] m_length;
        return;
    }

    // 分配Field向量存储所有行数据
    m_rows.resize(uint32(m_rowCount) * m_fieldCount);

    // 遍历所有行，读取数据
    while (_NextRow())
    {
        // 处理当前行的每个字段
        for (uint32 fIndex = 0; fIndex < m_fieldCount; ++fIndex)
        {
            // 设置字段元数据
            m_rows[uint32(m_rowPosition) * m_fieldCount + fIndex].SetMetadata(&m_fieldMetadata[fIndex]);

            unsigned long buffer_length = m_rBind[fIndex].buffer_length;
            unsigned long fetched_length = *m_rBind[fIndex].length;

            // 如果字段不为NULL
            if (!*m_rBind[fIndex].is_null)
            {
                void* buffer = m_stmt->bind[fIndex].buffer;

                // 处理字符串和BLOB类型
                switch (m_rBind[fIndex].buffer_type)
                {
                    case MYSQL_TYPE_TINY_BLOB:
                    case MYSQL_TYPE_MEDIUM_BLOB:
                    case MYSQL_TYPE_LONG_BLOB:
                    case MYSQL_TYPE_BLOB:
                    case MYSQL_TYPE_STRING:
                    case MYSQL_TYPE_VAR_STRING:
                        // 注意：当mysql_stmt_fetch返回MYSQL_DATA_TRUNCATED时
                        // 字符串可能没有null终止符
                        // 不能盲目添加null终止符，因为可能是二进制数据
                        // 使用Field::GetCString可能导致垃圾数据
                        // TODO: 移除Field::GetCString，在C++17中使用std::string_view
                        if (fetched_length < buffer_length)
                            *((char*)buffer + fetched_length) = '\0';
                        break;
                    default:
                        break;
                }

                // 设置字段值
                m_rows[uint32(m_rowPosition) * m_fieldCount + fIndex].SetValue(
                    (char const*)buffer,
                    fetched_length);

                // 移动缓冲区指针到下一行对应字段
                m_stmt->bind[fIndex].buffer = (char*)buffer + rowSize;
            }
            else
            {
                // 字段为NULL，设置空值
                m_rows[uint32(m_rowPosition) * m_fieldCount + fIndex].SetValue(
                    nullptr,
                    *m_rBind[fIndex].length);
            }
        }
        m_rowPosition++;
    }
    // 重置行位置索引
    m_rowPosition = 0;

    // 所有数据已缓冲，释放MySQL C API结构
    mysql_stmt_free_result(m_stmt);
}

/**
 * @brief ResultSet析构函数
 *
 * 调用CleanUp()清理MySQL结果集资源和Field数组。
 */
ResultSet::~ResultSet()
{
    CleanUp();
}

/**
 * @brief PreparedResultSet析构函数
 *
 * 调用CleanUp()清理MySQL资源和绑定缓冲区。
 */
PreparedResultSet::~PreparedResultSet()
{
    CleanUp();
}

/**
 * @brief 移动到结果集的下一行
 *
 * @return bool 成功读取下一行返回true，没有更多行或出错返回false
 *
 * 该方法从MySQL结果集中获取下一行数据，并更新_currentRow数组。
 * 使用mysql_fetch_row()从文本协议结果中读取数据。
 *
 * 执行流程：
 * 1. 检查结果集是否有效
 * 2. 调用mysql_fetch_row()获取下一行
 * 3. 如果没有更多行，清理资源并返回false
 * 4. 获取各字段长度
 * 5. 更新_currentRow数组中每个Field的值
 *
 * 调用时机：在循环中反复调用以遍历所有行
 * 性能注意事项：开销主要在MySQL C API调用和数据拷贝
 */
bool ResultSet::NextRow()
{
    MYSQL_ROW row;

    // 检查结果集是否有效
    if (!_result)
        return false;

    // 获取下一行数据
    row = mysql_fetch_row(_result);
    if (!row)
    {
        // 没有更多行，清理资源
        CleanUp();
        return false;
    }

    // 获取各字段的长度
    unsigned long* lengths = mysql_fetch_lengths(_result);
    if (!lengths)
    {
        TC_LOG_WARN("sql.sql", "{}:mysql_fetch_lengths, cannot retrieve value lengths. Error {}.", __FUNCTION__, mysql_error(_result->handle));
        CleanUp();
        return false;
    }

    // 更新_currentRow数组中每个Field的值
    for (uint32 i = 0; i < _fieldCount; i++)
        _currentRow[i].SetValue(row[i], lengths[i]);

    return true;
}

/**
 * @brief 移动到预处理结果集的下一行
 *
 * @return bool 成功返回true，已到末尾返回false
 *
 * 该方法只是更新行位置索引m_rowPosition，不执行实际的数据加载。
 * 所有数据在构造时已经加载到m_rows向量中。
 *
 * 调用时机：在循环中反复调用以遍历所有行
 * 性能注意事项：O(1)时间复杂度，只是递增索引
 */
bool PreparedResultSet::NextRow()
{
    // 只是更新行位置索引，让上层代码知道应该在m_rows向量的哪个位置查找数据
    if (++m_rowPosition >= m_rowCount)
        return false;

    return true;
}

/**
 * @brief 内部方法：从MySQL获取下一行数据
 *
 * @return bool 成功返回true，失败或无数据返回false
 *
 * 该方法仅在构造函数中调用，用于从MySQL服务器获取一行数据。
 * 使用mysql_stmt_fetch()从预处理语句结果中读取。
 *
 * 调用时机：仅在PreparedResultSet构造函数中调用
 * 性能注意事项：开销主要在MySQL C API调用
 */
bool PreparedResultSet::_NextRow()
{
    // 只在底层代码中调用，即构造函数
    // 遍历每一行数据并缓冲它
    if (m_rowPosition >= m_rowCount)
        return false;

    // 获取下一行数据
    // 返回值为0表示成功，MYSQL_DATA_TRUNCATED表示数据被截断但仍可用
    int retval = mysql_stmt_fetch(m_stmt);
    return retval == 0 || retval == MYSQL_DATA_TRUNCATED;
}

/**
 * @brief 清理ResultSet资源
 *
 * 释放MySQL结果集对象和Field数组内存。
 * 在遍历完成或出错时调用。
 */
void ResultSet::CleanUp()
{
    // 释放当前行Field数组
    if (_currentRow)
    {
        delete [] _currentRow;
        _currentRow = nullptr;
    }

    // 释放MySQL结果集对象
    if (_result)
    {
        mysql_free_result(_result);
        _result = nullptr;
    }
}

/**
 * @brief 数组访问运算符
 *
 * @param index 字段索引（从0开始）
 * @return Field const& 字段常量引用
 *
 * 通过索引访问当前行的字段，包含边界检查。
 */
Field const& ResultSet::operator[](std::size_t index) const
{
    ASSERT(index < _fieldCount);
    return _currentRow[index];
}

/**
 * @brief 获取当前行数据
 *
 * @return Field* 字段数组指针
 *
 * 返回当前行的字段数组指针，可通过索引访问各字段。
 * 包含行位置边界检查。
 */
Field* PreparedResultSet::Fetch() const
{
    ASSERT(m_rowPosition < m_rowCount);
    return const_cast<Field*>(&m_rows[uint32(m_rowPosition) * m_fieldCount]);
}

/**
 * @brief 数组访问运算符
 *
 * @param index 字段索引（从0开始）
 * @return Field const& 字段常量引用
 *
 * 通过索引访问当前行的字段，包含行位置和字段索引边界检查。
 */
Field const& PreparedResultSet::operator[](std::size_t index) const
{
    ASSERT(m_rowPosition < m_rowCount);
    ASSERT(index < m_fieldCount);
    return m_rows[uint32(m_rowPosition) * m_fieldCount + index];
}

/**
 * @brief 清理PreparedResultSet资源
 *
 * 释放MySQL元数据结果集和绑定缓冲区内存。
 */
void PreparedResultSet::CleanUp()
{
    // 释放MySQL元数据结果集
    if (m_metadataResult)
        mysql_free_result(m_metadataResult);

    // 释放绑定缓冲区和绑定结构
    if (m_rBind)
    {
        delete[](char*)m_rBind->buffer;  // 释放数据缓冲区
        delete[] m_rBind;                 // 释放绑定结构数组
        m_rBind = nullptr;
    }
}
