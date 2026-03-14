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
 * @file DatabaseWorker.cpp
 * @brief 数据库工作线程实现
 *
 * 本文件实现了 DatabaseWorker 类，提供数据库异步操作的工作线程功能。
 * 工作线程从共享队列中获取SQL任务并执行，实现数据库操作的异步处理。
 *
 * 核心机制：
 * - 生产者-消费者模式：主线程生产任务，工作线程消费执行
 * - 线程池模式：多个工作线程共享一个任务队列
 * - RAII 模式：构造启动线程，析构停止线程
 *
 * 性能考虑：
 * - 使用原子变量进行线程间通信，避免锁开销
 * - 任务执行完毕后立即删除，避免内存泄漏
 * - 线程在无任务时阻塞等待，不占用CPU
 */

#include "DatabaseWorker.h"
#include "SQLOperation.h"
#include "ProducerConsumerQueue.h"

/**
 * @brief 构造函数 - 初始化并启动工作线程
 * @param newQueue   任务队列指针，用于获取SQL操作任务
 * @param connection MySQL连接指针，执行SQL时使用的数据库连接
 *
 * @brief 初始化步骤：
 * 1. 保存队列和连接的引用
 * 2. 初始化取消标志为 false（线程应该继续运行）
 * 3. 创建并启动工作线程
 *
 * @note 工作线程立即开始执行 WorkerThread() 函数
 * @note 队列和连接必须在整个 DatabaseWorker 生命周期内有效
 */
DatabaseWorker::DatabaseWorker(ProducerConsumerQueue<SQLOperation*>* newQueue, MySQLConnection* connection)
{
    // 保存数据库连接引用，后续每个任务都会使用此连接执行
    _connection = connection;
    // 保存任务队列引用，从队列中获取待执行的SQL操作
    _queue = newQueue;
    // 初始化取消标志为 false，表示线程应继续运行
    _cancelationToken = false;
    // 创建并启动工作线程，执行 WorkerThread 函数
    _workerThread = std::thread(&DatabaseWorker::WorkerThread, this);
}

/**
 * @brief 析构函数 - 停止工作线程并等待其结束
 *
 * @brief 停止流程：
 * 1. 设置取消标志为 true，通知工作线程应该停止
 * 2. 调用队列的 Cancel() 方法，唤醒可能在 WaitAndPop() 中阻塞的线程
 * 3. 调用 join() 等待工作线程结束
 *
 * @note 必须先唤醒队列中的等待，否则工作线程可能永久阻塞
 * @note join() 确保线程完全结束后才继续析构，避免竞态条件
 */
DatabaseWorker::~DatabaseWorker()
{
    // 设置取消标志，通知工作线程应该退出
    _cancelationToken = true;

    // 取消队列，唤醒可能在 WaitAndPop() 中阻塞的线程
    // 这会让工作线程从阻塞状态返回，检查取消标志并退出
    _queue->Cancel();

    // 等待工作线程结束
    // 必须等待线程完全退出后才能继续析构
    _workerThread.join();
}

/**
 * @brief 工作线程主循环 - 持续从队列获取并执行SQL操作
 *
 * @brief 执行流程：
 * 1. 检查队列是否有效，无效则直接返回
 * 2. 进入无限循环：
 *    a. 从队列中等待并获取一个SQL操作任务（阻塞调用）
 *    b. 检查是否需要停止（取消标志或空操作）
 *    c. 为操作设置数据库连接
 *    d. 执行SQL操作
 *    e. 删除操作对象，释放内存
 *    f. 继续下一次循环
 *
 * @note WaitAndPop() 是阻塞调用，无任务时线程休眠，不消耗CPU
 * @note 每个操作执行完毕后立即删除，防止内存泄漏
 * @note 空操作（nullptr）或取消标志为 true 时退出循环
 *
 * 线程安全说明：
 * - _cancelationToken 是原子变量，多线程读写安全
 * - 队列操作是线程安全的
 * - 每个线程有自己的 MySQLConnection，不需要同步
 */
void DatabaseWorker::WorkerThread()
{
    // 安全检查：队列必须有效
    if (!_queue)
        return;

    // 主循环：持续获取并执行任务
    for (;;)
    {
        SQLOperation* operation = nullptr;

        // 从队列中等待并获取一个任务
        // 这是阻塞调用，无任务时线程休眠
        // 当有任务入队或队列被取消时返回
        _queue->WaitAndPop(operation);

        // 检查是否需要停止：
        // 1. 取消标志为 true（析构时设置）
        // 2. 获取的操作为空（队列被取消时可能返回空）
        if (_cancelationToken || !operation)
            return;

        // 为操作设置数据库连接
        // 每个操作需要知道在哪个连接上执行
        operation->SetConnection(_connection);

        // 执行SQL操作
        // call() 内部会调用 Execute() 或其他具体执行方法
        operation->call();

        // 删除操作对象，释放内存
        // 任务的生命周期到此结束
        // 注意：操作对象由工作线程负责删除
        delete operation;
    }
}
