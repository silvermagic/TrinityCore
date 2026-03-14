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
 * @file DatabaseWorker.h
 * @brief 数据库工作线程模块
 *
 * 本模块提供数据库工作线程的实现，负责从任务队列中取出SQL操作并执行。
 * 是数据库异步操作的核心组件。
 *
 * 核心功能：
 * - 管理独立的数据库工作线程
 * - 从生产者-消费者队列中获取SQL操作任务
 * - 执行SQL操作并管理其生命周期
 * - 支持优雅的线程停止
 *
 * 工作原理：
 * - 每个 DatabaseWorker 绑定一个 MySQLConnection
 * - 工作线程持续从队列中获取任务并执行
 * - 任务执行完毕后自动删除
 * - 析构时通知线程停止并等待其结束
 *
 * 并发模型：
 * - 多个工作线程共享同一个任务队列
 * - 使用线程安全的生产者-消费者队列
 * - 每个工作线程独立执行，互不干扰
 */

#ifndef _WORKERTHREAD_H
#define _WORKERTHREAD_H

#include "Define.h"
#include <atomic>
#include <thread>

template <typename T>
class ProducerConsumerQueue;

class MySQLConnection;
class SQLOperation;

/**
 * @class DatabaseWorker
 * @brief 数据库工作线程类 - 异步执行SQL操作的工作线程
 *
 * DatabaseWorker 是数据库异步操作体系的核心组件，负责执行数据库操作任务。
 * 每个工作线程绑定一个 MySQLConnection，从共享队列中获取任务并执行。
 *
 * 生命周期：
 * 1. 构造时启动工作线程
 * 2. 工作线程持续从队列获取任务并执行
 * 3. 析构时设置停止标志，唤醒队列，等待线程结束
 *
 * 使用场景：
 * - DatabaseWorkerPool 创建多个 DatabaseWorker 实例
 * - 每个 DatabaseWorker 独立运行，处理异步SQL操作
 * - 实现 SQL 操作与主线程的异步解耦
 *
 * 线程安全：
 * - 使用原子变量 _cancelationToken 进行线程间通信
 * - 队列本身是线程安全的
 * - 禁止拷贝和赋值，确保线程对象唯一性
 */
class TC_DATABASE_API DatabaseWorker
{
    public:
        /**
         * @brief 构造函数 - 创建并启动工作线程
         * @param newQueue   生产者-消费者队列指针，用于获取SQL操作任务
         * @param connection MySQL连接指针，工作线程绑定的数据库连接
         *
         * @brief 初始化流程：
         * 1. 保存队列和连接引用
         * 2. 初始化取消标志为 false
         * 3. 创建并启动工作线程（执行 WorkerThread 方法）
         *
         * @note 队列和连接的生命周期必须长于 DatabaseWorker 对象
         * @note 工作线程立即开始运行，等待队列中的任务
         */
        DatabaseWorker(ProducerConsumerQueue<SQLOperation*>* newQueue, MySQLConnection* connection);

        /**
         * @brief 析构函数 - 停止并等待工作线程结束
         *
         * @brief 停止流程：
         * 1. 设置取消标志为 true
         * 2. 调用队列的 Cancel() 方法，唤醒可能在等待的线程
         * 3. 等待工作线程结束（join）
         *
         * @note 确保线程安全退出，避免资源泄漏
         * @note 必须在队列和连接销毁之前析构 DatabaseWorker
         */
        ~DatabaseWorker();

    private:
        ProducerConsumerQueue<SQLOperation*>* _queue;  ///< 任务队列指针，线程共享
        MySQLConnection* _connection;                   ///< 数据库连接指针，线程绑定

        /**
         * @brief 工作线程主函数
         *
         * @brief 执行循环：
         * 1. 从队列中等待并获取一个SQL操作任务（阻塞）
         * 2. 检查取消标志，如果需要停止则退出
         * 3. 为操作设置数据库连接
         * 4. 执行SQL操作
         * 5. 删除操作对象（释放内存）
         * 6. 继续循环
         *
         * @note 这是工作线程的入口函数，在独立线程中运行
         * @note WaitAndPop() 会阻塞直到有任务或队列被取消
         * @note 每个操作执行完毕后立即删除，确保资源释放
         */
        void WorkerThread();

        std::thread _workerThread;      ///< 工作线程对象
        std::atomic<bool> _cancelationToken;  ///< 原子取消标志，用于线程间通信

        // 禁止拷贝和赋值，确保线程对象唯一性
        DatabaseWorker(DatabaseWorker const& right) = delete;
        DatabaseWorker& operator=(DatabaseWorker const& right) = delete;
};

#endif
