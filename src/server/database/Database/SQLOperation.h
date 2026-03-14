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
 * @file SQLOperation.h
 * @brief SQL 操作基类及相关数据结构定义
 *
 * 本文件定义了数据库操作系统的核心基础设施，包括：
 * - SQL 元素数据结构（用于存储原始查询或预处理语句）
 * - SQLOperation 基类（所有数据库操作的抽象基类）
 *
 * 这些组件是 TrinityCore 异步数据库系统的核心，支持同步和异步数据库操作。
 * 数据库操作会被封装成 SQLOperation 对象，然后通过数据库工作线程池执行。
 *
 * @see DatabaseWorkerPool
 * @see MySQLConnection
 */

#ifndef _SQLOPERATION_H
#define _SQLOPERATION_H

#include "Define.h"
#include "DatabaseEnvFwd.h"

/**
 * @union SQLElementUnion
 * @brief SQL 元素联合体，用于保存 SQL 查询或预处理语句
 *
 * 该联合体用于在同一个内存位置存储不同类型的 SQL 元素，
 * 可以是原始 SQL 查询字符串或预处理语句对象。
 * 通过联合体可以节省内存，因为同一时间只会使用其中一种类型。
 *
 * @note 联合体的大小等于最大成员的大小（指针大小）
 */
union SQLElementUnion
{
    PreparedStatementBase* stmt;    ///< 预处理语句指针，用于参数化查询
    char const* query;              ///< 原始 SQL 查询字符串指针
};

/**
 * @enum SQLElementDataType
 * @brief SQL 元素数据类型枚举
 *
 * 用于标识 SQLElementUnion 中当前存储的数据类型，
 * 确保正确地访问联合体中的数据。
 */
enum SQLElementDataType
{
    SQL_ELEMENT_RAW,        ///< 原始 SQL 查询，使用 query 字段
    SQL_ELEMENT_PREPARED    ///< 预处理语句，使用 stmt 字段
};

/**
 * @struct SQLElementData
 * @brief SQL 元素数据结构
 *
 * 该结构体封装了 SQL 查询的完整信息，包括查询内容本身和类型标识。
 * 这是数据库操作系统中传递 SQL 查询的基本数据单元。
 *
 * 使用示例：
 * @code
 * SQLElementData data;
 * data.element.query = "SELECT * FROM accounts";
 * data.type = SQL_ELEMENT_RAW;
 * @endcode
 *
 * 或使用预处理语句：
 * @code
 * SQLElementData data;
 * data.element.stmt = preparedStatement;
 * data.type = SQL_ELEMENT_PREPARED;
 * @endcode
 */
struct SQLElementData
{
    SQLElementUnion element;    ///< SQL 元素联合体，存储实际的查询数据
    SQLElementDataType type;    ///< 元素类型，标识联合体中存储的数据类型
};

class MySQLConnection;

/**
 * @class SQLOperation
 * @brief SQL 操作抽象基类
 *
 * 这是所有数据库操作的基类，定义了数据库操作的执行接口。
 * 该类采用命令模式（Command Pattern），将数据库操作封装为对象，
 * 使操作可以被排队、延迟执行或在不同的线程中执行。
 *
 * 主要特性：
 * - 纯虚接口：强制派生类实现 Execute() 方法
 * - 连接管理：通过 SetConnection() 设置执行操作的数据库连接
 * - 禁止拷贝：防止操作对象被意外复制
 * - 异步友好：可在线程池中异步执行
 *
 * 典型的派生类包括：
 * - BasicStatementTask: 执行原始 SQL 查询
 * - PreparedStatementTask: 执行预处理语句
 * - TransactionTask: 执行事务操作
 *
 * 使用流程：
 * 1. 创建具体的操作对象（如 PreparedStatementTask）
 * 2. 调用 SetConnection() 设置数据库连接
 * 3. 调用 call() 或 Execute() 执行操作
 *
 * @note 该类不能直接实例化，必须通过派生类实现
 * @warning 派生类必须实现 Execute() 方法
 */
class TC_DATABASE_API SQLOperation
{
    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化数据库连接指针为 nullptr。
         * 连接应在执行前通过 SetConnection() 设置。
         */
        SQLOperation(): m_conn(nullptr) { }

        /**
         * @brief 虚析构函数
         *
         * 确保派生类的对象能够正确释放资源。
         */
        virtual ~SQLOperation() { }

        /**
         * @brief 调用操作执行
         * @return 返回 0，表示执行成功
         *
         * 这是一个包装方法，内部调用 Execute() 执行实际的数据库操作。
         * 返回值设计为与线程池任务接口兼容。
         *
         * @note 默认实现总是返回 0，派生类可以重写此行为
         */
        virtual int call()
        {
            Execute();
            return 0;
        }

        /**
         * @brief 执行数据库操作（纯虚函数）
         * @return 操作是否成功执行
         *
         * 这是执行数据库操作的核心方法，必须在派生类中实现。
         * 实现应使用 m_conn 连接执行相应的 SQL 操作。
         *
         * @return true 操作执行成功
         * @return false 操作执行失败
         *
         * @note 这是纯虚函数，必须在派生类中实现
         */
        virtual bool Execute() = 0;

        /**
         * @brief 设置数据库连接
         * @param con MySQL 连接指针
         *
         * 设置执行此操作所需的数据库连接。
         * 应在执行操作之前调用此方法。
         *
         * @param con 指向 MySQLConnection 对象的指针，
         *           该连接将用于执行 SQL 操作
         *
         * @note 操作本身不拥有连接的所有权，
         *       连接的生命周期由调用者管理
         */
        virtual void SetConnection(MySQLConnection* con) { m_conn = con; }

    protected:
        MySQLConnection* m_conn;    ///< 用于执行操作的数据库连接指针

    private:
        /**
         * @brief 禁用拷贝构造函数
         *
         * 防止操作对象被拷贝，确保每个操作对象都是唯一的。
         * 这对于异步操作特别重要，避免多次执行同一操作。
         */
        SQLOperation(SQLOperation const& right) = delete;

        /**
         * @brief 禁用赋值运算符
         *
         * 防止操作对象被赋值，确保操作对象的唯一性和状态一致性。
         */
        SQLOperation& operator=(SQLOperation const& right) = delete;
};

#endif
