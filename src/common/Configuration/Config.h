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
 * @file Config.h
 * @brief 配置管理模块头文件
 *
 * 本文件定义了 TrinityCore 服务器的配置管理器类 ConfigMgr。
 *
 * 模块职责：
 *   - 加载和解析 INI 格式的配置文件
 *   - 提供类型安全的配置项访问接口
 *   - 支持多配置文件加载和热重载
 *   - 支持环境变量覆盖配置值
 *   - 提供线程安全的配置访问
 *
 * 主要功能：
 *   1. 配置文件加载：支持主配置文件和额外配置文件
 *   2. 配置目录加载：递归加载目录中的所有 .conf 文件
 *   3. 热重载：支持运行时重新加载配置文件
 *   4. 环境变量支持：通过环境变量覆盖配置值（格式：TC_配置键名）
 *   5. 类型转换：自动将字符串配置值转换为 bool、int、float 等类型
 *   6. 线程安全：使用互斥锁保护配置数据访问
 *
 * 使用方式：
 *   // 获取单例实例
 *   ConfigMgr* config = ConfigMgr::instance();
 *
 *   // 加载配置文件
 *   std::string error;
 *   config->LoadInitial("worldserver.conf", args, error);
 *
 *   // 读取配置值
 *   int port = config->GetIntDefault("WorldServerPort", 8085);
 *   bool debug = config->GetBoolDefault("Debug", false);
 *   std::string ip = config->GetStringDefault("BindIP", "0.0.0.0");
 *
 * 设计模式：
 *   - 单例模式：全局唯一的配置管理器实例
 *   - 外观模式：简化 boost::property_tree 的使用
 *
 * @see Config.cpp 实现文件
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "Define.h"
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief 配置管理器类 - 负责服务器配置文件的加载、解析和管理
 *
 * ConfigMgr 是一个单例类，用于管理 TrinityCore 服务器的所有配置信息。
 * 支持加载多个配置文件、环境变量覆盖、热重载等功能。
 * 配置值通过键值对存储，并提供类型安全的访问接口。
 */
class TC_COMMON_API ConfigMgr
{
    /**
     * @name 构造函数和析构函数（私有化以实现单例模式）
     * @{
     */
    ConfigMgr() = default;                              ///< 默认构造函数 - 私有化以实现单例模式
    ConfigMgr(ConfigMgr const&) = delete;               ///< 禁用拷贝构造函数
    ConfigMgr& operator=(ConfigMgr const&) = delete;    ///< 禁用赋值运算符
    ~ConfigMgr() = default;                             ///< 默认析构函数
    /** @} */

public:
    /**
     * @brief 加载初始主配置文件
     *
     * 仅用于加载主配置文件（authserver.conf 和 worldserver.conf）
     *
     * @param file 配置文件路径
     * @param args 命令行参数列表
     * @param error 错误信息输出参数
     * @return true 加载成功
     * @return false 加载失败
     */
    bool LoadInitial(std::string file, std::vector<std::string> args, std::string& error);

    /**
     * @brief 加载额外的配置文件
     *
     * @param file 配置文件路径
     * @param keepOnReload 热重载时是否保留该配置
     * @param error 错误信息输出参数
     * @return true 加载成功
     * @return false 加载失败
     */
    bool LoadAdditionalFile(std::string file, bool keepOnReload, std::string& error);

    /**
     * @brief 加载配置目录中的所有配置文件
     *
     * @param dir 配置目录路径
     * @param keepOnReload 热重载时是否保留这些配置
     * @param loadedFiles 已加载文件列表输出参数
     * @param errors 错误信息列表输出参数
     * @return true 加载成功
     * @return false 加载失败
     */
    bool LoadAdditionalDir(std::string const& dir, bool keepOnReload, std::vector<std::string>& loadedFiles, std::vector<std::string>& errors);

    /**
     * @brief 使用环境变量覆盖配置值
     *
     * 检查并使用环境变量覆盖已加载的配置项
     *
     * @return std::vector<std::string> 被覆盖的配置键列表
     */
    std::vector<std::string> OverrideWithEnvVariablesIfAny();

    /**
     * @brief 获取单例实例
     *
     * @return ConfigMgr* 配置管理器单例指针
     */
    static ConfigMgr* instance();

    /**
     * @brief 重新加载所有配置文件
     *
     * @param errors 错误信息列表输出参数
     * @return true 重载成功
     * @return false 重载失败
     */
    bool Reload(std::vector<std::string>& errors);

    /**
     * @brief 获取字符串类型配置值
     *
     * @param name 配置项名称
     * @param def 默认值
     * @param quiet 是否静默模式（不输出警告日志）
     * @return std::string 配置值或默认值
     */
    std::string GetStringDefault(std::string const& name, const std::string& def, bool quiet = false) const;

    /**
     * @brief 获取布尔类型配置值
     *
     * @param name 配置项名称
     * @param def 默认值
     * @param quiet 是否静默模式（不输出警告日志）
     * @return true 配置值为真
     * @return false 配置值为假或使用默认值
     */
    bool GetBoolDefault(std::string const& name, bool def, bool quiet = false) const;

    /**
     * @brief 获取整数类型配置值
     *
     * @param name 配置项名称
     * @param def 默认值
     * @param quiet 是否静默模式（不输出警告日志）
     * @return int 配置值或默认值
     */
    int GetIntDefault(std::string const& name, int def, bool quiet = false) const;

    /**
     * @brief 获取浮点数类型配置值
     *
     * @param name 配置项名称
     * @param def 默认值
     * @param quiet 是否静默模式（不输出警告日志）
     * @return float 配置值或默认值
     */
    float GetFloatDefault(std::string const& name, float def, bool quiet = false) const;

    /**
     * @brief 获取主配置文件名
     *
     * @return std::string const& 配置文件路径引用
     */
    std::string const& GetFilename();

    /**
     * @brief 获取命令行参数列表
     *
     * @return std::vector<std::string> const& 参数列表常量引用
     */
    std::vector<std::string> const& GetArguments() const;

    /**
     * @brief 根据字符串前缀获取所有匹配的配置键名
     *
     * @param name 配置键名前缀
     * @return std::vector<std::string> 匹配的配置键列表
     */
    std::vector<std::string> GetKeysByString(std::string const& name);

private:
    /**
     * @brief 获取配置值的通用模板方法
     *
     * 内部使用的模板方法，根据类型 T 自动转换配置值
     *
     * @tparam T 配置值类型（支持 bool、int、float、string）
     * @param name 配置项名称
     * @param def 默认值
     * @param quiet 是否静默模式
     * @return T 配置值或默认值
     */
    template<class T>
    T GetValueDefault(std::string const& name, T def, bool quiet) const;
};

/**
 * @brief 配置管理器全局访问宏
 *
 * 使用此宏可以方便地访问 ConfigMgr 单例实例。
 *
 * 使用示例：
 *   std::string value = sConfigMgr->GetStringDefault("SomeKey", "default");
 *   int port = sConfigMgr->GetIntDefault("Port", 8085);
 */
#define sConfigMgr ConfigMgr::instance()

#endif
