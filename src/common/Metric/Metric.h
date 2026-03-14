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
 * @file Metric.h
 * @brief 性能指标收集系统头文件
 *
 * 本模块实现了TrinityCore的性能指标收集系统，用于监控服务器运行状态和性能数据。
 *
 * 模块职责：
 *   - 收集服务器性能指标（如处理时间、队列长度、在线玩家数等）
 *   - 批量发送指标数据到InfluxDB时序数据库
 *   - 支持两种指标类型：数值型指标和事件型指标
 *   - 提供定时器和阈值过滤功能，优化性能数据收集
 *
 * 主要功能：
 *   1. 数值指标收集：记录各种性能数值（如函数执行时间、计数器等）
 *   2. 事件指标收集：记录重要事件（如服务器启动、玩家登录等）
 *   3. 批量数据发送：使用HTTP协议批量发送数据到InfluxDB
 *   4. 定时调度：定期发送数据并记录总体状态
 *   5. 阈值过滤：只记录达到阈值的指标，减少数据量
 *
 * 性能指标收集原理：
 *   - 使用无锁MPSC队列（多生产者单消费者）收集指标数据，保证线程安全和高性能
 *   - 数据以InfluxDB Line Protocol格式组织：<measurement>[,<tag>=<value>] <field>=<value> <timestamp>
 *   - 批量发送减少网络开销，提高吞吐量
 *   - 使用RAII计时器自动测量代码块执行时间
 *   - 通过宏定义实现条件编译，可在编译时完全禁用指标收集
 *
 * 使用示例：
 *   @code
 *   // 记录数值指标
 *   TC_METRIC_VALUE("player_count", onlinePlayers);
 *
 *   // 记录执行时间
 *   TC_METRIC_TIMER("update_time");
 *
 *   // 记录事件
 *   TC_METRIC_EVENT("server", "startup", "World server started");
 *   @endcode
 */

#ifndef METRIC_H__
#define METRIC_H__

#include "Define.h"
#include "Duration.h"
#include "MPSCQueue.h"
#include "Optional.h"
#include <boost/container/small_vector.hpp>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace Trinity
{
    namespace Asio
    {
        class IoContext;        ///< 异步I/O上下文类，用于定时器和网络操作
        class DeadlineTimer;    ///< 截止时间定时器类，用于调度任务
    }
}

/**
 * @enum MetricDataType
 * @brief 指标数据类型枚举
 *
 * 定义指标数据的类型，区分数值型指标和事件型指标
 */
enum MetricDataType
{
    METRIC_DATA_VALUE,   ///< 数值型指标：记录性能数值（如执行时间、计数等）
    METRIC_DATA_EVENT    ///< 事件型指标：记录重要事件（如服务器启动、玩家登录等）
};

/// @brief 指标标签类型：键值对，用于为指标添加元数据
using MetricTag = std::pair<std::string, std::string>;

/// @brief 指标标签向量：存储多个标签，使用小向量优化（默认容量2）
using MetricTagsVector = boost::container::small_vector<MetricTag, 2>;

/**
 * @struct MetricData
 * @brief 指标数据结构体
 *
 * 存储单个指标数据的所有信息，包括类别、时间戳、类型和具体数据。
 * 该结构体用于无锁队列中传递指标数据。
 */
struct MetricData
{
    std::string Category;           ///< 指标类别名称（InfluxDB中的measurement）
    SystemTimePoint Timestamp;      ///< 时间戳（系统时钟时间点）
    MetricDataType Type;            ///< 数据类型（数值或事件）

    // LogValue-specific fields
    MetricTagsVector Tags;          ///< 标签列表：为指标添加维度信息

    // LogEvent-specific fields
    std::string Title;              ///< 事件标题：仅用于事件型指标

    std::string ValueOrEventText;   ///< 数值或事件文本：数值型存储格式化后的值，事件型存储描述文本

    // intrusive queue link
    std::atomic<MetricData*> QueueLink;  ///< 入侵式队列链接指针：用于MPSC队列
};

/**
 * @class Metric
 * @brief 性能指标收集系统核心类
 *
 * 实现了TrinityCore的指标收集、批处理和发送功能。
 * 该类作为单例使用，通过sMetric宏访问。
 *
 * 核心特性：
 *   - 使用MPSC队列实现高性能多线程指标收集
 *   - 批量发送数据到InfluxDB，减少网络开销
 *   - 支持定时器和阈值过滤，优化性能
 *   - 自动处理网络连接和重连
 *
 * 工作流程：
 *   1. 各线程调用LogValue/LogEvent记录指标到MPSC队列
 *   2. 定时器定期触发SendBatch批量发送数据
 *   3. 数据通过HTTP协议发送到InfluxDB数据库
 *   4. 总体状态定时器定期记录服务器整体状态
 */
class TC_COMMON_API Metric
{
private:
    /**
     * @brief 获取数据流引用
     * @return std::iostream& TCP数据流的引用
     */
    std::iostream& GetDataStream() { return *_dataStream; }

    std::unique_ptr<std::iostream> _dataStream;     ///< TCP数据流，用于与InfluxDB通信
    MPSCQueue<MetricData, &MetricData::QueueLink> _queuedData;  ///< 无锁MPSC队列，存储待发送的指标数据
    std::unique_ptr<Trinity::Asio::DeadlineTimer> _batchTimer;  ///< 批量发送定时器
    std::unique_ptr<Trinity::Asio::DeadlineTimer> _overallStatusTimer;  ///< 总体状态记录定时器
    int32 _updateInterval = 0;                      ///< 批量发送间隔（秒）
    int32 _overallStatusTimerInterval = 0;          ///< 总体状态记录间隔（秒）
    bool _enabled = false;                          ///< 指标系统启用状态
    bool _overallStatusTimerTriggered = false;      ///< 总体状态定时器触发标志
    std::string _hostname;                          ///< InfluxDB服务器主机名
    std::string _port;                              ///< InfluxDB服务器端口
    std::string _databaseName;                      ///< InfluxDB数据库名称
    std::function<void()> _overallStatusLogger;     ///< 总体状态记录回调函数
    std::string _realmName;                         ///< 服务器领域名称（用作标签）
    std::unordered_map<std::string, int64> _thresholds;  ///< 阈值映射：类别->阈值

    /**
     * @brief 连接到InfluxDB服务器
     * @return bool 连接成功返回true，失败返回false
     */
    bool Connect();

    /**
     * @brief 批量发送指标数据到InfluxDB
     *
     * 从队列中取出所有指标数据，按InfluxDB Line Protocol格式组装，
     * 通过HTTP POST请求发送到数据库
     */
    void SendBatch();

    /**
     * @brief 调度批量发送任务
     *
     * 根据启用状态调度或取消批量发送任务
     */
    void ScheduleSend();

    /**
     * @brief 调度总体状态记录任务
     *
     * 定期触发总体状态记录回调函数
     */
    void ScheduleOverallStatusLog();

    /**
     * @brief 格式化布尔值为InfluxDB格式
     * @param value 布尔值
     * @return std::string InfluxDB格式的字符串（"t"或"f"）
     */
    static std::string FormatInfluxDBValue(bool value);

    /**
     * @brief 格式化整型值为InfluxDB格式
     * @tparam T 整型类型
     * @param value 整型值
     * @return std::string InfluxDB格式的字符串（带'i'后缀）
     */
    template <class T>
    static std::string FormatInfluxDBValue(T value);

    /**
     * @brief 格式化字符串为InfluxDB格式
     * @param value 字符串值
     * @return std::string InfluxDB格式的字符串（用双引号包围并转义）
     */
    static std::string FormatInfluxDBValue(std::string const& value);

    /**
     * @brief 格式化C风格字符串为InfluxDB格式
     * @param value C风格字符串指针
     * @return std::string InfluxDB格式的字符串
     */
    static std::string FormatInfluxDBValue(char const* value);

    /**
     * @brief 格式化双精度浮点数为InfluxDB格式
     * @param value 双精度浮点数
     * @return std::string InfluxDB格式的字符串
     */
    static std::string FormatInfluxDBValue(double value);

    /**
     * @brief 格式化单精度浮点数为InfluxDB格式
     * @param value 单精度浮点数
     * @return std::string InfluxDB格式的字符串
     */
    static std::string FormatInfluxDBValue(float value);

    /**
     * @brief 格式化时间间隔为InfluxDB格式
     * @param value 纳秒级时间间隔
     * @return std::string InfluxDB格式的字符串（转换为毫秒）
     */
    static std::string FormatInfluxDBValue(std::chrono::nanoseconds value);

    /**
     * @brief 格式化InfluxDB标签值
     * @param value 标签值字符串
     * @return std::string 转义后的标签值
     */
    static std::string FormatInfluxDBTagValue(std::string const& value);

    // ToDo: should format TagKey and FieldKey too in the same way as TagValue

public:
    /**
     * @brief 默认构造函数
     */
    Metric();

    /**
     * @brief 析构函数
     */
    ~Metric();

    /**
     * @brief 获取Metric单例实例
     * @return Metric* 指向单例实例的指针
     */
    static Metric* instance();

    /**
     * @brief 初始化指标系统
     * @param realmName 服务器领域名称
     * @param ioContext 异步I/O上下文
     * @param overallStatusLogger 总体状态记录回调函数
     */
    void Initialize(std::string const& realmName, Trinity::Asio::IoContext& ioContext, std::function<void()> overallStatusLogger);

    /**
     * @brief 从配置文件加载设置
     *
     * 加载指标系统的配置参数，包括启用状态、连接信息、阈值等
     */
    void LoadFromConfigs();

    /**
     * @brief 更新指标系统
     *
     * 在主循环中定期调用，执行总体状态记录
     */
    void Update();

    /**
     * @brief 判断是否应该记录指定类别的指标
     * @param category 指标类别名称
     * @param value 指标数值
     * @return bool 如果值达到或超过阈值返回true
     */
    bool ShouldLog(std::string const& category, int64 value) const;

    /**
     * @brief 记录数值型指标
     *
     * 创建数值型指标数据并加入队列等待发送
     *
     * @tparam T 数值类型
     * @tparam Tags 标签类型
     * @param category 指标类别名称
     * @param value 指标数值
     * @param tags 附加标签（可选）
     */
    template<class T, class... Tags>
    void LogValue(std::string category, T value, Tags&&... tags)
    {
        using namespace std::chrono;

        // 分配新的指标数据对象
        MetricData* data = new MetricData;
        data->Category = std::move(category);
        data->Timestamp = system_clock::now();  // 记录当前时间戳
        data->Type = METRIC_DATA_VALUE;         // 设置为数值型指标
        data->ValueOrEventText = FormatInfluxDBValue(value);  // 格式化数值

        // 如果有标签参数，使用折叠表达式添加到标签向量
        if constexpr (sizeof...(tags) > 0)
            (data->Tags.emplace_back(std::move(tags)), ...);

        // 将数据加入MPSC队列，多线程安全
        _queuedData.Enqueue(data);
    }

    /**
     * @brief 记录事件型指标
     * @param category 事件类别
     * @param title 事件标题
     * @param description 事件描述
     */
    void LogEvent(std::string category, std::string title, std::string description);

    /**
     * @brief 卸载指标系统
     *
     * 在系统关闭时发送剩余数据并清理资源
     */
    void Unload();

    /**
     * @brief 检查指标系统是否启用
     * @return bool 启用返回true
     */
    bool IsEnabled() const { return _enabled; }
};

/// @brief 全局Metric单例访问宏
#define sMetric Metric::instance()

/**
 * @class MetricStopWatch
 * @brief 指标计时器RAII类
 *
 * 使用RAII模式自动测量代码块的执行时间。
 * 构造时开始计时，析构时自动调用记录函数。
 *
 * @tparam LoggerType 记录器函数类型
 *
 * 使用示例：
 *   @code
 *   {
 *       auto watch = MakeMetricStopWatch([&](TimePoint start) {
 *           sMetric->LogValue("my_timer", std::chrono::steady_clock::now() - start);
 *       });
 *       // ... 执行需要计时的代码 ...
 *   } // 析构时自动记录执行时间
 *   @endcode
 */
template<typename LoggerType>
class MetricStopWatch
{
public:
    /**
     * @brief 构造函数，开始计时
     * @param loggerFunc 记录器回调函数，在析构时调用
     */
    MetricStopWatch(LoggerType&& loggerFunc) :
        _logger(std::forward<LoggerType>(loggerFunc)),
        _startTime(std::chrono::steady_clock::now())
    {
    }

    /**
     * @brief 析构函数，自动调用记录器
     *
     * 将开始时间传递给记录器函数，记录器计算执行时间并记录指标
     */
    ~MetricStopWatch()
    {
        _logger(_startTime);
    }

private:
    LoggerType _logger;         ///< 记录器回调函数
    TimePoint _startTime;       ///< 开始时间点
};

/**
 * @brief 创建指标计时器的工厂函数
 *
 * 如果指标系统未启用，返回空Optional避免性能开销
 *
 * @tparam LoggerType 记录器类型
 * @param loggerFunc 记录器回调函数
 * @return Optional<MetricStopWatch<LoggerType>> 计时器对象或空值
 */
template<typename LoggerType>
Optional<MetricStopWatch<LoggerType>> MakeMetricStopWatch(LoggerType&& loggerFunc)
{
    // 如果指标系统未启用，返回空Optional避免创建计时器
    if (!sMetric->IsEnabled())
        return {};

    // 创建并返回计时器对象
    return Optional<MetricStopWatch<LoggerType>>(std::in_place, std::forward<LoggerType>(loggerFunc));
}

/// @brief 创建指标标签的辅助宏
#define TC_METRIC_TAG(name, value) MetricTag(name, value)

/// @brief 宏拼接辅助宏（内部使用）
#define TC_METRIC_DO_CONCAT(a, b) a ## b

/// @brief 宏拼接辅助宏（内部使用）
#define TC_METRIC_CONCAT(a, b) TC_METRIC_DO_CONCAT(a, b)

/// @brief 生成唯一变量名的辅助宏
#define TC_METRIC_UNIQUE_NAME(name) TC_METRIC_CONCAT(name, __LINE__)

#if defined PERFORMANCE_PROFILING || defined WITHOUT_METRICS
/**
 * 性能分析或禁用指标模式下的空宏定义
 * 这些宏在编译时被替换为空操作，完全移除指标收集代码
 */
#define TC_METRIC_EVENT(category, title, description) ((void)0)
#define TC_METRIC_VALUE(category, value, ...) ((void)0)
#define TC_METRIC_TIMER(category, ...) ((void)0)
#define TC_METRIC_DETAILED_EVENT(category, title, description) ((void)0)
#define TC_METRIC_DETAILED_TIMER(category, ...) ((void)0)
#define TC_METRIC_DETAILED_NO_THRESHOLD_TIMER(category, ...) ((void)0)
#else
#  if TRINITY_PLATFORM != TRINITY_PLATFORM_WINDOWS
/**
 * @brief 记录事件指标的宏（非Windows平台）
 *
 * @param category 事件类别
 * @param title 事件标题
 * @param description 事件描述
 *
 * 使用示例：
 *   TC_METRIC_EVENT("server", "startup", "World server started successfully");
 */
#define TC_METRIC_EVENT(category, title, description)                  \
        do {                                                           \
            if (sMetric->IsEnabled())                                  \
                sMetric->LogEvent(category, title, description);       \
        } while (0)

/**
 * @brief 记录数值指标的宏（非Windows平台）
 *
 * @param category 指标类别
 * @param value 指标数值
 * @param ... 可选的标签参数
 *
 * 使用示例：
 *   TC_METRIC_VALUE("player_count", onlinePlayers);
 *   TC_METRIC_VALUE("latency", ms, TC_METRIC_TAG("region", "us"));
 */
#define TC_METRIC_VALUE(category, value, ...)                          \
        do {                                                           \
            if (sMetric->IsEnabled())                                  \
                sMetric->LogValue(category, value, ##__VA_ARGS__);     \
        } while (0)
#  else
/**
 * @brief 记录事件指标的宏（Windows平台）
 *
 * Windows版本需要禁用编译器警告C4127（条件表达式为常量）
 */
#define TC_METRIC_EVENT(category, title, description)                  \
        __pragma(warning(push))                                        \
        __pragma(warning(disable:4127))                                \
        do {                                                           \
            if (sMetric->IsEnabled())                                  \
                sMetric->LogEvent(category, title, description);       \
        } while (0)                                                    \
        __pragma(warning(pop))

/**
 * @brief 记录数值指标的宏（Windows平台）
 *
 * Windows版本需要禁用编译器警告C4127（条件表达式为常量）
 */
#define TC_METRIC_VALUE(category, value, ...)                          \
        __pragma(warning(push))                                        \
        __pragma(warning(disable:4127))                                \
        do {                                                           \
            if (sMetric->IsEnabled())                                  \
                sMetric->LogValue(category, value, ##__VA_ARGS__);     \
        } while (0)                                                    \
        __pragma(warning(pop))
#  endif

/**
 * @brief 记录代码块执行时间的宏
 *
 * 使用RAII计时器自动测量从当前行到作用域结束的执行时间
 *
 * @param category 指标类别
 * @param ... 可选的标签参数
 *
 * 使用示例：
 *   {
 *       TC_METRIC_TIMER("update_time");
 *       // ... 需要计时的代码 ...
 *   } // 离开作用域时自动记录执行时间
 */
#define TC_METRIC_TIMER(category, ...)                                                                           \
        auto TC_METRIC_UNIQUE_NAME(__tc_metric_stop_watch) = MakeMetricStopWatch([&](TimePoint start)            \
        {                                                                                                        \
            sMetric->LogValue(category, std::chrono::steady_clock::now() - start, ##__VA_ARGS__);                \
        });

#  if defined WITH_DETAILED_METRICS
/**
 * @brief 记录详细执行时间指标的宏（带阈值过滤）
 *
 * 只有当执行时间达到配置的阈值时才记录指标，减少数据量
 *
 * @param category 指标类别
 * @param ... 可选的标签参数
 *
 * 使用示例：
 *   {
 *       TC_METRIC_DETAILED_TIMER("slow_operation");
 *       // ... 可能较慢的操作 ...
 *   } // 仅在执行时间超过阈值时记录
 */
#define TC_METRIC_DETAILED_TIMER(category, ...)                                                                  \
        auto TC_METRIC_UNIQUE_NAME(__tc_metric_stop_watch) = MakeMetricStopWatch([&](TimePoint start)            \
        {                                                                                                        \
            int64 duration = int64(std::chrono::duration_cast<Milliseconds>(std::chrono::steady_clock::now() - start).count()); \
            std::string category2 = category;                                                                    \
            if (sMetric->ShouldLog(category2, duration))                                                         \
                sMetric->LogValue(std::move(category2), duration, ##__VA_ARGS__);                                \
        });

/**
 * @brief 记录详细执行时间指标的宏（无阈值过滤）
 *
 * 与TC_METRIC_TIMER功能相同，在详细指标模式下启用
 */
#define TC_METRIC_DETAILED_NO_THRESHOLD_TIMER(category, ...) TC_METRIC_TIMER(category, ##__VA_ARGS__)

/**
 * @brief 记录详细事件指标的宏
 *
 * 在详细指标模式下启用，用于记录重要事件
 */
#define TC_METRIC_DETAILED_EVENT(category, title, description) TC_METRIC_EVENT(category, title, description)
#  else
/**
 * 详细指标未启用时的空宏定义
 * 这些宏在非详细指标模式下被替换为空操作
 */
#define TC_METRIC_DETAILED_EVENT(category, title, description) ((void)0)
#define TC_METRIC_DETAILED_TIMER(category, ...) ((void)0)
#define TC_METRIC_DETAILED_NO_THRESHOLD_TIMER(category, ...) ((void)0)
#  endif

#endif

#endif // METRIC_H__
