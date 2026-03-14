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
 * @file PreparedStatement.cpp
 * @brief 预处理语句实现文件
 *
 * 本文件实现了数据库预处理语句的相关功能，包括：
 * - PreparedStatementBase: 预处理语句基类，提供参数绑定功能
 * - PreparedStatementTask: 预处理语句执行任务，用于异步数据库操作
 * - PreparedStatementData: 预处理语句数据辅助类，提供数据转换为字符串的功能
 *
 * 预处理语句是数据库操作的核心组件，提供以下优势：
 * - 防止 SQL 注入攻击
 * - 提高查询性能（数据库可以缓存执行计划）
 * - 类型安全的参数绑定
 * - 支持异步执行和结果获取
 */

#include "PreparedStatement.h"
#include "Errors.h"
#include "MySQLConnection.h"
#include "MySQLPreparedStatement.h"
#include "QueryResult.h"
#include "Log.h"
#include "MySQLWorkaround.h"
#include <fmt/chrono.h>

/**
 * @brief 构造函数，初始化预处理语句基类
 *
 * 创建一个预处理语句对象，预留指定数量的参数位置
 *
 * @param index 预处理语句索引，用于在数据库连接中查找对应的 SQL 语句
 * @param capacity 参数容量，即该预处理语句需要绑定的参数数量
 */
PreparedStatementBase::PreparedStatementBase(uint32 index, uint8 capacity) :
m_index(index), statement_data(capacity) { }

/**
 * @brief 析构函数
 *
 * 默认析构函数，清理预处理语句资源
 */
PreparedStatementBase::~PreparedStatementBase() { }

/**
 * @defgroup PreparedStatementBindings 参数绑定方法
 * @brief 预处理语句参数绑定函数组
 *
 * 这些方法用于将不同类型的值绑定到预处理语句的参数位置。
 * 每个方法都会检查索引是否有效，然后将值存储到参数数据数组中。
 * @{
 */

/**
 * @brief 绑定布尔值到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的布尔值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setBool(uint8 index, bool value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定无符号 8 位整数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 uint8 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setUInt8(uint8 index, uint8 value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定无符号 16 位整数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 uint16 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setUInt16(uint8 index, uint16 value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定无符号 32 位整数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 uint32 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setUInt32(uint8 index, uint32 value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定无符号 64 位整数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 uint64 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setUInt64(uint8 index, uint64 value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定有符号 8 位整数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 int8 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setInt8(uint8 index, int8 value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定有符号 16 位整数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 int16 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setInt16(uint8 index, int16 value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定有符号 32 位整数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 int32 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setInt32(uint8 index, int32 value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定有符号 64 位整数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 int64 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setInt64(uint8 index, int64 value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定单精度浮点数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 float 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setFloat(uint8 index, float value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定双精度浮点数到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的 double 值
 * @note 断言检查索引是否在有效范围内
 */
void PreparedStatementBase::setDouble(uint8 index, double value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定日期时间值到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的系统时间点值（SystemTimePoint）
 * @note 断言检查索引是否在有效范围内
 * @note SystemTimePoint 是 std::chrono::system_clock::time_point 的别名
 */
void PreparedStatementBase::setDate(uint8 index, SystemTimePoint value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定字符串到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的字符串常量引用
 * @note 断言检查索引是否在有效范围内
 * @note 字符串会被复制存储到参数数据中
 */
void PreparedStatementBase::setString(uint8 index, std::string const& value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 绑定字符串视图到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的字符串视图
 * @note 断言检查索引是否在有效范围内
 * @note 字符串视图会被转换为字符串存储，避免悬垂引用
 */
void PreparedStatementBase::setStringView(uint8 index, std::string_view value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data.emplace<std::string>(value);
}

/**
 * @brief 绑定二进制数据到指定参数位置
 *
 * @param index 参数索引（从 0 开始）
 * @param value 要绑定的二进制数据向量常量引用
 * @note 断言检查索引是否在有效范围内
 * @note 用于存储二进制大对象（BLOB）或字节数组
 */
void PreparedStatementBase::setBinary(uint8 index, std::vector<uint8> const& value)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = value;
}

/**
 * @brief 将指定参数位置设置为 NULL
 *
 * @param index 参数索引（从 0 开始）
 * @note 断言检查索引是否在有效范围内
 * @note 用于显式设置参数为数据库 NULL 值
 */
void PreparedStatementBase::setNull(uint8 index)
{
    ASSERT(index < statement_data.size());
    statement_data[index].data = nullptr;
}

/** @} */ // end of PreparedStatementBindings

/**
 * @defgroup PreparedStatementExecution 预处理语句执行
 * @brief 预处理语句执行任务相关方法
 *
 * PreparedStatementTask 用于在数据库工作线程中执行预处理语句，
 * 支持同步和异步两种执行模式。
 * @{
 */

/**
 * @brief 构造函数，创建预处理语句执行任务
 *
 * @param stmt 预处理语句指针（任务获取所有权，会在析构时删除）
 * @param async 是否为异步执行模式
 *
 * 异步模式下会创建一个 Promise 对象，用于在其他线程中获取查询结果。
 * 同步模式则直接执行，不返回结果。
 */
PreparedStatementTask::PreparedStatementTask(PreparedStatementBase* stmt, bool async) :
m_stmt(stmt), m_result(nullptr)
{
    m_has_result = async; // 如果是异步模式，则需要返回结果
    if (async)
        m_result = new PreparedQueryResultPromise();
}

/**
 * @brief 析构函数，清理执行任务资源
 *
 * 删除预处理语句对象和结果 Promise 对象（如果存在）。
 * 确保所有分配的资源都被正确释放。
 */
PreparedStatementTask::~PreparedStatementTask()
{
    delete m_stmt;
    if (m_has_result && m_result != nullptr)
        delete m_result;
}

/**
 * @brief 执行预处理语句任务
 *
 * 在数据库工作线程中调用，执行预处理语句。
 *
 * @return 执行成功返回 true，失败返回 false
 *
 * 执行模式：
 * - 异步模式：执行查询并将结果存储到 Promise 中，供其他线程获取
 * - 同步模式：直接执行语句（通常是 INSERT/UPDATE/DELETE），不返回结果集
 *
 * 对于异步查询，如果结果集为空或查询失败，会设置空结果并返回 false。
 */
bool PreparedStatementTask::Execute()
{
    if (m_has_result)
    {
        // 异步查询模式：执行查询并存储结果
        PreparedResultSet* result = m_conn->Query(m_stmt);
        if (!result || !result->GetRowCount())
        {
            // 查询失败或结果为空，设置空结果并返回失败
            delete result;
            m_result->set_value(PreparedQueryResult(nullptr));
            return false;
        }
        // 查询成功，设置结果
        m_result->set_value(PreparedQueryResult(result));
        return true;
    }

    // 同步执行模式：直接执行语句（不返回结果集）
    return m_conn->Execute(m_stmt);
}

/** @} */ // end of PreparedStatementExecution

/**
 * @defgroup PreparedStatementDataHelpers 预处理语句数据辅助函数
 * @brief 参数数据转换为字符串的辅助函数组
 *
 * 这些函数用于将预处理语句的参数值转换为字符串表示，
 * 主要用于调试日志和错误信息输出。
 * @{
 */

/**
 * @brief 通用模板函数，将任意类型值转换为字符串
 *
 * @tparam T 值的类型
 * @param value 要转换的值
 * @return 值的字符串表示
 *
 * 使用 fmt 库进行格式化，支持所有可格式化的类型。
 */
template<typename T>
std::string PreparedStatementData::ToString(T value)
{
    return fmt::format("{}", value);
}

/**
 * @brief 将布尔值转换为字符串
 *
 * @param value 布尔值
 * @return 字符串表示（转换为 uint32 后再转字符串，即 "0" 或 "1"）
 */
std::string PreparedStatementData::ToString(bool value)
{
    return ToString<uint32>(value);
}

/**
 * @brief 将 uint8 值转换为字符串
 *
 * @param value uint8 值
 * @return 字符串表示（转换为 uint32 后再转字符串，避免被当作字符处理）
 */
std::string PreparedStatementData::ToString(uint8 value)
{
    return ToString<uint32>(value);
}

// 模板显式实例化：uint16、uint32、uint64 类型的 ToString 函数
template std::string PreparedStatementData::ToString<uint16>(uint16);
template std::string PreparedStatementData::ToString<uint32>(uint32);
template std::string PreparedStatementData::ToString<uint64>(uint64);

/**
 * @brief 将 int8 值转换为字符串
 *
 * @param value int8 值
 * @return 字符串表示（转换为 int32 后再转字符串，避免被当作字符处理）
 */
std::string PreparedStatementData::ToString(int8 value)
{
    return ToString<int32>(value);
}

// 模板显式实例化：int16、int32、int64、float、double 类型的 ToString 函数
template std::string PreparedStatementData::ToString<int16>(int16);
template std::string PreparedStatementData::ToString<int32>(int32);
template std::string PreparedStatementData::ToString<int64>(int64);
template std::string PreparedStatementData::ToString<float>(float);
template std::string PreparedStatementData::ToString<double>(double);

/**
 * @brief 将字符串值转换为带引号的字符串表示
 *
 * @param value 字符串常量引用
 * @return 带单引号的字符串表示（如 "'hello'"）
 *
 * 为字符串添加单引号，便于在 SQL 日志中区分字符串值。
 */
std::string PreparedStatementData::ToString(std::string const& value)
{
    return Trinity::StringFormat("'{}'", value);
}

/**
 * @brief 将二进制数据转换为字符串表示
 *
 * @param value 二进制数据向量（未使用）
 * @return 固定字符串 "BINARY"
 *
 * 二进制数据不适合直接显示，返回固定标识符。
 */
std::string PreparedStatementData::ToString(std::vector<uint8> const& /*value*/)
{
    return "BINARY";
}

/**
 * @brief 将系统时间点转换为日期时间字符串
 *
 * @param value 系统时间点值
 * @return 格式化的日期时间字符串（格式：YYYY-MM-DD HH:MM:SS）
 *
 * 使用 fmt 库的 chrono 扩展进行格式化。
 */
std::string PreparedStatementData::ToString(SystemTimePoint value)
{
    return Trinity::StringFormat("{:%F %T}", value);
}

/**
 * @brief 将 nullptr 转换为字符串表示
 *
 * @param value nullptr 值（未使用）
 * @return 固定字符串 "NULL"
 *
 * 用于表示数据库 NULL 值。
 */
std::string PreparedStatementData::ToString(std::nullptr_t)
{
    return "NULL";
}

/** @} */ // end of PreparedStatementDataHelpers
