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
 * @file Metric.cpp
 * @brief 性能指标收集系统实现文件
 *
 * 本文件实现了TrinityCore的性能指标收集系统的核心功能。
 *
 * 模块职责：
 *   - 管理与InfluxDB数据库的TCP连接
 *   - 实现指标数据的批量发送机制
 *   - 处理配置文件的加载和动态更新
 *   - 提供InfluxDB Line Protocol格式化功能
 *   - 管理定时器调度（批量发送和状态记录）
 *
 * 主要功能：
 *   1. 网络通信：
 *      - 使用Boost.Asio建立TCP连接到InfluxDB
 *      - 通过HTTP POST请求发送数据
 *      - 处理连接失败和自动重连
 *
 *   2. 数据批处理：
 *      - 从MPSC队列中取出指标数据
 *      - 按InfluxDB Line Protocol格式组装
 *      - 批量发送以减少网络开销
 *
 *   3. 定时调度：
 *      - 定期发送批量数据（可配置间隔）
 *      - 定期记录服务器整体状态
 *
 *   4. 数据格式化：
 *      - 支持多种数据类型（整数、浮点数、字符串、时间等）
 *      - 实现InfluxDB特殊字符转义
 *      - 处理标签和字段的格式化
 *
 * InfluxDB Line Protocol格式：
 *   <measurement>[,<tag_key>=<tag_value>...] <field_key>=<field_value>[,<field_key>=<field_value>...] [<timestamp>]
 *
 *   示例：
 *   player_count,realm=MyRealm value=150i 1609459200000000000
 *   update_time,realm=MyRealm value=42i 1609459200000000000
 *   server,realm=MyRealm title="startup",text="World server started" 1609459200000000000
 *
 * 性能优化策略：
 *   - 使用MPSC无锁队列，避免锁竞争
 *   - 批量发送减少网络往返次数
 *   - 阈值过滤减少不重要的指标记录
 *   - 异步I/O避免阻塞主线程
 *   - 条件编译支持完全移除指标代码
 *
 * 配置项：
 *   - Metric.Enable: 是否启用指标系统
 *   - Metric.ConnectionInfo: InfluxDB连接信息（格式：主机;端口;数据库名）
 *   - Metric.Interval: 批量发送间隔（秒）
 *   - Metric.OverallStatusInterval: 总体状态记录间隔（秒）
 *   - Metric.Threshold.<类别>: 指定类别的阈值设置
 */

#include "Metric.h"
#include "Common.h"
#include "Config.h"
#include "DeadlineTimer.h"
#include "Log.h"
#include "Strand.h"
#include "Util.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/asio/ip/tcp.hpp>

/**
 * @brief 初始化性能指标系统
 *
 * 职责:
 *   初始化指标系统的核心组件,包括数据流、定时器和状态记录器,
 *   并从配置文件加载相关设置
 *
 * 参数:
 *   realmName - 服务器领域名称,用作InfluxDB的标签
 *   ioContext - 异步I/O上下文,用于定时器调度
 *   overallStatusLogger - 总体状态记录回调函数
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 创建TCP数据流对象
 *   2. 格式化并存储领域名称
 *   3. 创建批量发送定时器和总体状态定时器
 *   4. 保存状态记录回调函数
 *   5. 从配置文件加载所有设置
 */
void Metric::Initialize(std::string const& realmName, Trinity::Asio::IoContext& ioContext, std::function<void()> overallStatusLogger)
{
    _dataStream = std::make_unique<boost::asio::ip::tcp::iostream>();
    _realmName = FormatInfluxDBTagValue(realmName);
    _batchTimer = std::make_unique<Trinity::Asio::DeadlineTimer>(ioContext);
    _overallStatusTimer = std::make_unique<Trinity::Asio::DeadlineTimer>(ioContext);
    _overallStatusLogger = overallStatusLogger;
    LoadFromConfigs();
}

/**
 * @brief 连接到InfluxDB服务器
 *
 * 职责:
 *   建立与InfluxDB数据库服务器的TCP连接
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   bool - 连接成功返回true,连接失败返回false
 *
 * 主要流程:
 *   1. 获取TCP数据流引用
 *   2. 尝试连接到配置的主机和端口
 *   3. 检查连接错误
 *   4. 如果连接失败,记录错误日志并禁用指标系统
 *   5. 如果连接成功,清除错误状态并返回true
 */
bool Metric::Connect()
{
    auto& stream = static_cast<boost::asio::ip::tcp::iostream&>(GetDataStream());
    stream.connect(_hostname, _port);
    auto error = stream.error();
    if (error)
    {
        TC_LOG_ERROR("metric", "Error connecting to '{}:{}', disabling Metric. Error message : {}",
            _hostname, _port, error.message());
        _enabled = false;
        return false;
    }
    stream.clear();
    return true;
}

/**
 * @brief 从配置文件加载指标系统设置
 *
 * 职责:
 *   从配置文件中加载指标系统的各项配置参数,
 *   包括启用状态、更新间隔、阈值设置等,并根据配置状态启动或停止定时器
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 读取并保存之前的启用状态
 *   2. 从配置读取启用状态和更新间隔(最小为1秒)
 *   3. 读取总体状态记录间隔(最小为1秒)
 *   4. 清空并重新加载所有阈值配置
 *   5. 如果从禁用变为启用:
 *      - 解析连接信息(格式:主机;端口;数据库名)
 *      - 连接到InfluxDB服务器
 *      - 启动批量发送定时器和总体状态定时器
 */
void Metric::LoadFromConfigs()
{
    // 保存之前的启用状态，用于检测状态变化
    bool previousValue = _enabled;

    // 读取启用状态配置
    _enabled = sConfigMgr->GetBoolDefault("Metric.Enable", false);

    // 读取批量发送间隔，最小为1秒
    _updateInterval = sConfigMgr->GetIntDefault("Metric.Interval", 1);
    if (_updateInterval < 1)
    {
        TC_LOG_ERROR("metric", "'Metric.Interval' config set to {}, overriding to 1.", _updateInterval);
        _updateInterval = 1;
    }

    // 读取总体状态记录间隔，最小为1秒
    _overallStatusTimerInterval = sConfigMgr->GetIntDefault("Metric.OverallStatusInterval", 1);
    if (_overallStatusTimerInterval < 1)
    {
        TC_LOG_ERROR("metric", "'Metric.OverallStatusInterval' config set to {}, overriding to 1.", _overallStatusTimerInterval);
        _overallStatusTimerInterval = 1;
    }

    // 清空并重新加载所有阈值配置
    _thresholds.clear();
    std::vector<std::string> thresholdSettings = sConfigMgr->GetKeysByString("Metric.Threshold.");
    for (std::string const& thresholdSetting : thresholdSettings)
    {
        int thresholdValue = sConfigMgr->GetIntDefault(thresholdSetting, 0);
        // 提取类别名称（移除"Metric.Threshold."前缀）
        std::string thresholdName = thresholdSetting.substr(strlen("Metric.Threshold."));
        _thresholds[thresholdName] = thresholdValue;
    }

    // Schedule a send at this point only if the config changed from Disabled to Enabled.
    // Cancel any scheduled operation if the config changed from Enabled to Disabled.
    // 仅当配置从禁用变为启用时才调度发送任务
    // 如果配置从启用变为禁用，则取消已调度的操作
    if (_enabled && !previousValue)
    {
        // 读取连接信息（格式：主机;端口;数据库名）
        std::string connectionInfo = sConfigMgr->GetStringDefault("Metric.ConnectionInfo", "");
        if (connectionInfo.empty())
        {
            TC_LOG_ERROR("metric", "'Metric.ConnectionInfo' not specified in configuration file.");
            return;
        }

        // 解析连接信息（使用分号分隔）
        std::vector<std::string_view> tokens = Trinity::Tokenize(connectionInfo, ';', true);
        if (tokens.size() != 3)
        {
            TC_LOG_ERROR("metric", "'Metric.ConnectionInfo' specified with wrong format in configuration file.");
            return;
        }

        // 保存连接参数
        _hostname.assign(tokens[0]);
        _port.assign(tokens[1]);
        _databaseName.assign(tokens[2]);

        // 尝试连接到InfluxDB服务器
        Connect();

        // 启动批量发送定时器
        ScheduleSend();
        // 启动总体状态记录定时器
        ScheduleOverallStatusLog();
    }
}

/**
 * @brief 更新指标系统
 *
 * 职责:
 *   检查并执行总体状态记录回调,在主循环中定期调用
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 检查总体状态定时器是否触发
 *   2. 如果触发,重置标志位并调用状态记录回调函数
 */
void Metric::Update()
{
    if (_overallStatusTimerTriggered)
    {
        _overallStatusTimerTriggered = false;
        _overallStatusLogger();
    }
}

/**
 * @brief 判断是否应该记录指定类别的指标
 *
 * 职责:
 *   根据配置的阈值判断某个类别的指标值是否应该被记录,
 *   只有达到或超过阈值的指标才会被记录到系统
 *
 * 参数:
 *   category - 指标类别名称
 *   value - 指标的数值
 *
 * 返回值:
 *   bool - 如果值达到或超过阈值返回true,否则返回false
 *          如果类别未配置阈值也返回false
 *
 * 主要流程:
 *   1. 在阈值映射中查找指定类别
 *   2. 如果类别不存在,返回false
 *   3. 如果值大于等于阈值,返回true
 */
bool Metric::ShouldLog(std::string const& category, int64 value) const
{
    auto threshold = _thresholds.find(category);
    if (threshold == _thresholds.end())
        return false;
    return value >= threshold->second;
}

/**
 * @brief 记录事件类型指标
 *
 * 职责:
 *   创建并排队一个事件类型的指标数据,用于记录重要事件
 *   (如玩家登录、世界服务器启动等)
 *
 * 参数:
 *   category - 事件类别名称
 *   title - 事件标题
 *   description - 事件描述文本
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 分配新的指标数据对象
 *   2. 设置类别、时间戳、类型为事件类型
 *   3. 设置标题和描述文本
 *   4. 将数据对象加入队列等待批量发送
 */
void Metric::LogEvent(std::string category, std::string title, std::string description)
{
    using namespace std::chrono;

    MetricData* data = new MetricData;
    data->Category = std::move(category);
    data->Timestamp = system_clock::now();
    data->Type = METRIC_DATA_EVENT;
    data->Title = std::move(title);
    data->ValueOrEventText = std::move(description);

    _queuedData.Enqueue(data);
}

/**
 * @brief 批量发送指标数据到InfluxDB
 *
 * 职责:
 *   将队列中所有待发送的指标数据批量发送到InfluxDB数据库,
 *   使用HTTP协议的Line Protocol格式
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 从队列中取出所有指标数据
 *   2. 按InfluxDB Line Protocol格式批量组装数据:
 *      - 格式: <category>[,<tag>=<value>] <field>=<value> <timestamp>
 *      - 添加realm标签和自定义标签
 *      - 根据数据类型(数值/事件)格式化字段
 *      - 时间戳转换为纳秒
 *   3. 如果队列为空,直接调度下次发送
 *   4. 检查连接状态,必要时重新连接
 *   5. 通过HTTP POST请求发送数据到InfluxDB
 *   6. 检查响应状态码(期望204)
 *   7. 处理响应头,如果收到Connection: close则关闭连接
 *   8. 调度下次批量发送
 */
void Metric::SendBatch()
{
    using namespace std::chrono;

    std::stringstream batchedData;
    MetricData* data;
    bool firstLoop = true;

    // 从MPSC队列中取出所有待发送的指标数据
    while (_queuedData.Dequeue(data))
    {
        // 在数据行之间添加换行符（InfluxDB要求）
        if (!firstLoop)
            batchedData << "\n";

        // 写入指标类别（measurement）
        batchedData << data->Category;

        // 添加realm标签（如果配置了）
        if (!_realmName.empty())
            batchedData << ",realm=" << _realmName;

        // 添加所有自定义标签
        for (MetricTag const& tag : data->Tags)
            batchedData << "," << tag.first << "=" << FormatInfluxDBTagValue(tag.second);

        // 分隔标签和字段
        batchedData << " ";

        // 根据数据类型格式化字段
        switch (data->Type)
        {
            case METRIC_DATA_VALUE:
                // 数值型指标：字段格式为 value=<数值>
                batchedData << "value=" << data->ValueOrEventText;
                break;
            case METRIC_DATA_EVENT:
                // 事件型指标：字段格式为 title="...",text="..."
                batchedData << "title=\"" << data->Title << "\",text=\"" << data->ValueOrEventText << "\"";
                break;
        }

        // 分隔字段和时间戳
        batchedData << " ";

        // 添加纳秒级时间戳（InfluxDB要求）
        batchedData << std::to_string(duration_cast<nanoseconds>(data->Timestamp.time_since_epoch()).count());

        firstLoop = false;
        delete data;  // 释放指标数据对象
    }

    // 检查是否有数据需要发送
    if (batchedData.tellp() == std::streampos(0))
    {
        ScheduleSend();
        return;
    }

    // 检查连接状态，必要时重新连接
    if (!GetDataStream().good() && !Connect())
        return;

    // 发送HTTP POST请求头
    GetDataStream() << "POST " << "/write?db=" << _databaseName << " HTTP/1.1\r\n";
    GetDataStream() << "Host: " << _hostname << ":" << _port << "\r\n";
    GetDataStream() << "Accept: */*\r\n";
    GetDataStream() << "Content-Type: application/octet-stream\r\n";
    GetDataStream() << "Content-Transfer-Encoding: binary\r\n";

    // 发送Content-Length和空行（标识头部结束）
    GetDataStream() << "Content-Length: " << std::to_string(batchedData.tellp()) << "\r\n\r\n";
    // 发送实际数据（InfluxDB Line Protocol格式）
    GetDataStream() << batchedData.rdbuf();

    // 读取HTTP响应状态行
    std::string http_version;
    GetDataStream() >> http_version;
    unsigned int status_code = 0;
    GetDataStream() >> status_code;

    // InfluxDB成功响应应返回204 No Content
    if (status_code != 204)
    {
        TC_LOG_ERROR("metric", "Error sending data, returned HTTP code: {}", status_code);
    }

    // 读取并忽略状态描述
    std::string status_description;
    std::getline(GetDataStream(), status_description);

    // 读取响应头部
    std::string header;
    while (std::getline(GetDataStream(), header) && header != "\r")
    {
        // 如果服务器关闭连接，我们也关闭
        if (header == "Connection: close\r")
            static_cast<boost::asio::ip::tcp::iostream&>(GetDataStream()).close();
    }

    // 调度下一次批量发送
    ScheduleSend();
}

/**
 * @brief 调度批量发送任务
 *
 * 职责:
 *   根据指标系统启用状态,调度或取消批量发送任务,
 *   并在禁用时清理队列中的待发送数据
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   如果启用:
 *     1. 设置定时器在指定间隔后过期
 *     2. 异步等待并调用SendBatch发送数据
 *   如果禁用:
 *     1. 关闭TCP连接
 *     2. 清空队列中所有待发送数据并释放内存
 */
void Metric::ScheduleSend()
{
    if (_enabled)
    {
        _batchTimer->expires_from_now(boost::posix_time::seconds(_updateInterval));
        _batchTimer->async_wait(std::bind(&Metric::SendBatch, this));
    }
    else
    {
        static_cast<boost::asio::ip::tcp::iostream&>(GetDataStream()).close();
        MetricData* data;
        // Clear the queue
        while (_queuedData.Dequeue(data))
            delete data;
    }
}

/**
 * @brief 卸载指标系统
 *
 * 职责:
 *   在系统关闭时清理资源,发送剩余数据并取消定时器
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 检查是否启用且I/O上下文已停止(仅在关闭时)
 *   2. 如果满足条件,禁用系统并发送队列中剩余数据
 *   3. 取消批量发送定时器和总体状态定时器
 */
void Metric::Unload()
{
    // Send what's queued only if IoContext is stopped (so only on shutdown)
    if (_enabled && Trinity::Asio::get_io_context(*_batchTimer).stopped())
    {
        _enabled = false;
        SendBatch();
    }

    _batchTimer->cancel();
    _overallStatusTimer->cancel();
}

/**
 * @brief 调度总体状态记录任务
 *
 * 职责:
 *   定期调度总体状态记录回调,用于记录服务器整体运行状态
 *   (如在线玩家数、队列大小等)
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 检查系统是否启用
 *   2. 如果启用,设置定时器在指定间隔后过期
 *   3. 异步等待,触发时设置标志位并递归调度下一次
 *   4. Update()函数会在主循环中检查标志位并执行实际记录
 */
void Metric::ScheduleOverallStatusLog()
{
    if (_enabled)
    {
        _overallStatusTimer->expires_from_now(boost::posix_time::seconds(_overallStatusTimerInterval));
        _overallStatusTimer->async_wait([this](const boost::system::error_code&)
        {
            _overallStatusTimerTriggered = true;
            ScheduleOverallStatusLog();
        });
    }
}

/**
 * @brief 格式化布尔值为InfluxDB格式
 *
 * 职责:
 *   将布尔值转换为InfluxDB接受的格式("t"或"f")
 *
 * 参数:
 *   value - 要格式化的布尔值
 *
 * 返回值:
 *   std::string - "t"表示true,"f"表示false
 *
 * 主要流程:
 *   根据布尔值返回对应的字符串
 */
std::string Metric::FormatInfluxDBValue(bool value)
{
    return value ? "t" : "f";
}

/**
 * @brief 格式化整型值为InfluxDB格式
 *
 * 职责:
 *   将整型值转换为InfluxDB的整数格式(带'i'后缀)
 *
 * 参数:
 *   value - 要格式化的整型值
 *
 * 返回值:
 *   std::string - 整数的字符串表示,带'i'后缀(如"123i")
 *
 * 主要流程:
 *   将值转换为字符串并追加'i'字符
 */
template<class T>
std::string Metric::FormatInfluxDBValue(T value)
{
    return std::to_string(value) + 'i';
}

/**
 * @brief 格式化字符串为InfluxDB格式
 *
 * 职责:
 *   将字符串值转换为InfluxDB的字符串格式(用双引号包围并转义内部双引号)
 *
 * 参数:
 *   value - 要格式化的字符串
 *
 * 返回值:
 *   std::string - 用双引号包围且内部双引号已转义的字符串
 *
 * 主要流程:
 *   1. 将所有内部双引号转义为\"
 *   2. 用双引号包围整个字符串
 */
std::string Metric::FormatInfluxDBValue(std::string const& value)
{
    return '"' + boost::replace_all_copy(value, "\"", "\\\"") + '"';
}

/**
 * @brief 格式化C风格字符串为InfluxDB格式
 *
 * 职责:
 *   将C风格字符串转换为InfluxDB的字符串格式
 *
 * 参数:
 *   value - 要格式化的C风格字符串指针
 *
 * 返回值:
 *   std::string - 用双引号包围且内部双引号已转义的字符串
 *
 * 主要流程:
 *   将C风格字符串转换为std::string并调用字符串版本的重载函数
 */
std::string Metric::FormatInfluxDBValue(char const* value)
{
    return FormatInfluxDBValue(std::string(value));
}

/**
 * @brief 格式化双精度浮点数为InfluxDB格式
 *
 * 职责:
 *   将double值转换为InfluxDB的浮点数格式(不带'i'后缀)
 *
 * 参数:
 *   value - 要格式化的双精度浮点数
 *
 * 返回值:
 *   std::string - 浮点数的字符串表示
 *
 * 主要流程:
 *   将值转换为字符串(InfluxDB会自动识别为浮点数)
 */
std::string Metric::FormatInfluxDBValue(double value)
{
    return std::to_string(value);
}

/**
 * @brief 格式化单精度浮点数为InfluxDB格式
 *
 * 职责:
 *   将float值转换为InfluxDB的浮点数格式
 *
 * 参数:
 *   value - 要格式化的单精度浮点数
 *
 * 返回值:
 *   std::string - 浮点数的字符串表示
 *
 * 主要流程:
 *   转换为double后调用double版本的重载函数
 */
std::string Metric::FormatInfluxDBValue(float value)
{
    return FormatInfluxDBValue(double(value));
}

/**
 * @brief 格式化InfluxDB标签值
 *
 * 职责:
 *   将字符串值转换为InfluxDB标签格式,转义特殊字符
 *
 * 参数:
 *   value - 要格式化的标签值字符串
 *
 * 返回值:
 *   std::string - 转义后的标签值,空格已转义
 *
 * 主要流程:
 *   将所有空格转义为"\ "(注意:还需处理=和,字符)
 *
 * 注意:
 *   TODO: 应该同时处理'='和','字符
 */
std::string Metric::FormatInfluxDBTagValue(std::string const& value)
{
    // ToDo: should handle '=' and ',' characters too
    return boost::replace_all_copy(value, " ", "\\ ");
}

/**
 * @brief 格式化时间间隔为InfluxDB格式
 *
 * 职责:
 *   将纳秒级时间间隔转换为毫秒级的整数值格式
 *
 * 参数:
 *   value - 纳秒级时间间隔
 *
 * 返回值:
 *   std::string - 毫秒计数的字符串表示,带'i'后缀
 *
 * 主要流程:
 *   1. 将纳秒转换为毫秒
 *   2. 调用整型版本的格式化函数
 */
std::string Metric::FormatInfluxDBValue(std::chrono::nanoseconds value)
{
    return FormatInfluxDBValue(std::chrono::duration_cast<Milliseconds>(value).count());
}

/**
 * @brief 默认构造函数
 *
 * 职责:
 *   构造Metric对象,初始化为默认状态
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   使用默认构造(成员变量在Initialize中初始化)
 */
Metric::Metric()
{
}

/**
 * @brief 析构函数
 *
 * 职责:
 *   清理Metric对象资源
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   使用默认析构(智能指针自动管理资源)
 */
Metric::~Metric()
{
}

/**
 * @brief 获取Metric单例实例
 *
 * 职责:
 *   提供全局访问点,返回Metric系统的单例实例
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   Metric* - 指向单例实例的指针
 *
 * 主要流程:
 *   使用静态局部变量实现线程安全的单例模式
 */
Metric* Metric::instance()
{
    static Metric instance;
    return &instance;
}

// 显式实例化模板函数，支持各种整型类型
// 这些实例化使得模板函数可以在其他编译单元中使用
template TC_COMMON_API std::string Metric::FormatInfluxDBValue(int8);
template TC_COMMON_API std::string Metric::FormatInfluxDBValue(uint8);
template TC_COMMON_API std::string Metric::FormatInfluxDBValue(int16);
template TC_COMMON_API std::string Metric::FormatInfluxDBValue(uint16);
template TC_COMMON_API std::string Metric::FormatInfluxDBValue(int32);
template TC_COMMON_API std::string Metric::FormatInfluxDBValue(uint32);
template TC_COMMON_API std::string Metric::FormatInfluxDBValue(int64);
template TC_COMMON_API std::string Metric::FormatInfluxDBValue(uint64);
