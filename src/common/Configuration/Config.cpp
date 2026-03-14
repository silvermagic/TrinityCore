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
 * @file Config.cpp
 * @brief 配置管理模块实现文件
 *
 * 本文件实现了 TrinityCore 服务器的配置管理器类 ConfigMgr。
 *
 * 模块职责：
 *   - 解析和管理 INI 格式配置文件
 *   - 提供类型安全的配置值访问接口
 *   - 实现配置文件热重载机制
 *   - 支持环境变量覆盖配置值
 *   - 提供线程安全的配置访问
 *
 * 核心实现细节：
 *   1. 使用 boost::property_tree 存储 INI 配置项
 *   2. 使用 std::mutex 保护配置数据访问（线程安全）
 *   3. 支持嵌套配置键（使用'/'分隔符）
 *   4. 环境变量命名规则：TC_转换后的键名（例如：WorldServerPort -> TC_WORLD_SERVER_PORT）
 *
 * 主要函数实现：
 *   - LoadFile()：加载并解析 INI 文件
 *   - LoadInitial()：加载主配置文件
 *   - LoadAdditionalFile()：加载额外配置文件
 *   - LoadAdditionalDir()：递归加载配置目录
 *   - OverrideWithEnvVariablesIfAny()：环境变量覆盖
 *   - Reload()：重新加载所有配置
 *   - GetValueDefault()：模板函数，类型安全地获取配置值
 *
 * 技术要点：
 *   - INI 文件解析：使用 boost::property_tree::ini_parser
 *   - 错误处理：捕获解析错误并生成详细错误信息
 *   - 环境变量转换：智能转换配置键名（处理驼峰命名、数字等）
 *   - 类型转换：使用 Trinity::StringTo 进行字符串到目标类型的转换
 *
 * @see Config.h 头文件声明
 */

#include "Config.h"
#include "Log.h"
#include "StringConvert.h"
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>

// 命名空间别名：简化 boost::property_tree 的使用
namespace bpt = boost::property_tree;
namespace fs  = boost::filesystem;

/**
 * @brief 匿名命名空间 - 包含全局配置变量和辅助函数
 *
 * 这些变量和函数仅在当前编译单元内可见，用于支持 ConfigMgr 的实现。
 * 所有配置数据都存储在这个命名空间中，确保数据的封装性。
 */
namespace
{
    /**
     * @name 全局配置变量
     * @brief 存储配置文件路径、内容和访问控制
     * @{
     */

    std::string _filename;                          ///< 主配置文件名（如：worldserver.conf）
    std::vector<std::string> _additonalFiles;       ///< 额外配置文件列表（重载时需要重新加载）
    std::vector<std::string> _args;                 ///< 命令行参数列表（在重载时保持不变）
    bpt::ptree _config;                             ///< 配置属性树（使用boost::property_tree存储所有配置项）
    std::mutex _configLock;                         ///< 配置访问互斥锁（保证多线程环境下的线程安全）

    /** @} */ // 结束全局配置变量组

    /**
     * @brief 加载并解析INI配置文件
     *
     * 职责：
     *   读取指定路径的INI格式配置文件，解析为属性树结构
     *
     * 参数：
     *   @param file     - 配置文件的完整路径
     *   @param fullTree - 输出参数，解析后的属性树
     *   @param error    - 输出参数，错误信息（失败时填充）
     *
     * 返回值：
     *   bool - 加载成功返回true，失败返回false
     *
     * 主要流程：
     *   1. 使用boost::property_tree解析INI文件
     *   2. 检查文件是否为空
     *   3. 捕获解析错误并生成详细错误信息
     */
    bool LoadFile(std::string const& file, bpt::ptree& fullTree, std::string& error)
    {
        try
        {
            // 使用 boost::property_tree 解析 INI 格式配置文件
            bpt::ini_parser::read_ini(file, fullTree);

            // 检查配置文件是否为空
            if (fullTree.empty())
            {
                error = "empty file (" + file + ")";
                return false;
            }
        }
        catch (bpt::ini_parser::ini_parser_error const& e)
        {
            // 捕获 INI 解析错误并生成详细的错误信息
            if (e.line() == 0)
                // 文件级别错误（如文件不存在）
                error = e.message() + " (" + e.filename() + ")";
            else
                // 行级别错误（如语法错误），包含行号信息
                error = e.message() + " (" + e.filename() + ":" + std::to_string(e.line()) + ")";
            return false;
        }

        return true;
    }

    /**
     * @brief 将INI配置键转换为环境变量键（大写下划线格式）
     *
     * 职责：
     *   将配置文件中的键名转换为符合环境变量命名规则的格式
     *
     * 参数：
     *   @param key - INI配置键名
     *
     * 返回值：
     *   std::string - 转换后的环境变量键名
     *
     * 主要流程：
     *   1. 将空格、点号、连字符转换为下划线
     *   2. 处理驼峰命名：在大小写转换边界插入下划线（如：aB -> A_B）
     *   3. 处理字母与数字边界：插入下划线（如：a1 -> A_1, 1a -> 1_A）
     *   4. 所有字符转换为大写
     *
     * 转换示例：
     *   SomeConfig => SOME_CONFIG
     *   myNestedConfig.opt1 => MY_NESTED_CONFIG_OPT_1
     *   LogDB.Opt.ClearTime => LOG_DB_OPT_CLEAR_TIME
     */
    std::string IniKeyToEnvVarKey(std::string const& key)
    {
        std::string result;  // 存储转换后的环境变量键名

        const char *str = key.c_str();
        size_t n = key.length();

        char curr;           // 当前处理的字符
        bool isEnd;          // 是否到达字符串末尾
        bool nextIsUpper;    // 下一个字符是否为大写
        bool currIsNumeric;  // 当前字符是否为数字
        bool nextIsNumeric;  // 下一个字符是否为数字

        // 遍历键名的每个字符
        for (size_t i = 0; i < n; ++i)
        {
            curr = str[i];

            // 将空格、点号、连字符统一转换为下划线
            if (curr == ' ' || curr == '.' || curr == '-')
            {
                result += '_';
                continue;
            }

            isEnd = i == n - 1;
            if (!isEnd)
            {
                nextIsUpper = isupper(str[i + 1]);

                // 处理驼峰命名边界：小写字母后跟大写字母时插入下划线
                // 例如：aB -> A_B, someConfig -> SOME_CONFIG
                if (!isupper(curr) && nextIsUpper)
                {
                    result += static_cast<char>(std::toupper(curr));
                    result += '_';
                    continue;
                }

                currIsNumeric = isNumeric(curr);
                nextIsNumeric = isNumeric(str[i + 1]);

                // 处理字母与数字边界：字母后跟数字时插入下划线
                // 例如：a1 -> A_1, config1 -> CONFIG_1
                if (!currIsNumeric && nextIsNumeric)
                {
                    result += static_cast<char>(std::toupper(curr));
                    result += '_';
                    continue;
                }

                // 处理数字与字母边界：数字后跟字母时插入下划线
                // 例如：1a -> 1_A, v1Config -> V_1_CONFIG
                if (currIsNumeric && !nextIsNumeric)
                {
                    result += static_cast<char>(std::toupper(curr));
                    result += '_';
                    continue;
                }
            }

            // 默认情况：将当前字符转换为大写
            result += static_cast<char>(std::toupper(curr));
        }
        return result;
    }

    /**
     * @brief 获取INI键对应的环境变量值
     *
     * 职责：
     *   查找指定配置键对应的环境变量，环境变量名格式为：TC_转换后的键名
     *
     * 参数：
     *   @param key - INI配置键名
     *
     * 返回值：
     *   Optional<std::string> - 存在则返回环境变量值，否则返回空
     *
     * 主要流程：
     *   1. 将键名转换为环境变量格式（添加TC_前缀）
     *   2. 调用std::getenv获取环境变量值
     *   3. 返回找到的值或空Optional
     */
    Optional<std::string> EnvVarForIniKey(std::string const& key)
    {
        // 构建环境变量键名：添加 TC_ 前缀并转换为环境变量格式
        std::string envKey = "TC_" + IniKeyToEnvVarKey(key);

        // 获取环境变量值
        char* val = std::getenv(envKey.c_str());
        if (!val)
            return std::nullopt;  // 环境变量不存在

        return std::string(val);
    }
}

/**
 * @brief 加载主配置文件并初始化配置管理器
 *
 * 职责：
 *   加载主配置文件，存储命令行参数，初始化配置属性树
 *
 * 参数：
 *   @param file  - 主配置文件路径
 *   @param args  - 命令行参数列表
 *   @param error - 输出参数，错误信息（失败时填充）
 *
 * 返回值：
 *   bool - 加载成功返回true，失败返回false
 *
 * 主要流程：
 *   1. 获取配置锁（线程安全）
 *   2. 保存文件名和命令行参数
 *   3. 调用LoadFile解析INI文件
 *   4. 提取第一个section的内容作为配置（TrinityCore使用单section配置）
 */
bool ConfigMgr::LoadInitial(std::string file, std::vector<std::string> args,
                            std::string& error)
{
    // 获取配置锁，保证线程安全
    std::lock_guard<std::mutex> lock(_configLock);

    // 保存配置文件名和命令行参数
    _filename = std::move(file);
    _args = std::move(args);

    // 加载并解析配置文件
    bpt::ptree fullTree;
    if (!LoadFile(_filename, fullTree, error))
        return false;

    // 提取第一个 section 的内容作为配置
    // TrinityCore 使用单 section 配置文件，因此直接跳过 section 名称
    // 例如：[worldserver] section 下的所有配置项会被提取到 _config 中
    _config = fullTree.begin()->second;

    return true;
}

/**
 * @brief 加载额外的配置文件
 *
 * 职责：
 *   加载附加配置文件，并将其内容合并到主配置树中
 *
 * 参数：
 *   @param file         - 额外配置文件路径
 *   @param keepOnReload - 是否在重载配置时保留该文件（true=重载时重新加载此文件）
 *   @param error        - 输出参数，错误信息（失败时填充）
 *
 * 返回值：
 *   bool - 加载成功返回true，失败返回false
 *
 * 主要流程：
 *   1. 调用LoadFile解析INI文件
 *   2. 获取配置锁
 *   3. 遍历新配置的所有节点，合并到主配置树中
 *   4. 如果keepOnReload为true，将文件名添加到额外文件列表
 */
bool ConfigMgr::LoadAdditionalFile(std::string file, bool keepOnReload, std::string& error)
{
    // 加载并解析额外配置文件
    bpt::ptree fullTree;
    if (!LoadFile(file, fullTree, error))
        return false;

    // 获取配置锁，保证线程安全
    std::lock_guard<std::mutex> lock(_configLock);

    // 遍历额外配置文件中的所有配置项，合并到主配置树中
    // 使用 '/' 作为路径分隔符，支持嵌套配置项
    for (bpt::ptree::value_type const& child : fullTree.begin()->second)
        _config.put_child(bpt::ptree::path_type(child.first, '/'), child.second);

    // 如果标记为需要保留，则将文件名添加到额外文件列表
    // 在调用 Reload() 时会重新加载这些文件
    if (keepOnReload)
        _additonalFiles.emplace_back(std::move(file));

    return true;
}

/**
 * @brief 递归加载目录中的所有配置文件
 *
 * 职责：
 *   遍历指定目录及其子目录，加载所有.conf扩展名的配置文件
 *
 * 参数：
 *   @param dir           - 配置文件目录路径
 *   @param keepOnReload  - 是否在重载配置时保留这些文件
 *   @param loadedFiles   - 输出参数，成功加载的文件列表
 *   @param errors        - 输出参数，加载失败的错误信息列表
 *
 * 返回值：
 *   bool - 所有文件加载成功返回true，有错误返回false
 *
 * 主要流程：
 *   1. 检查目录是否存在且为有效目录
 *   2. 递归遍历目录中的所有文件
 *   3. 筛选.conf扩展名的文件
 *   4. 调用LoadAdditionalFile加载每个配置文件
 *   5. 收集成功和失败的文件信息
 */
bool ConfigMgr::LoadAdditionalDir(std::string const& dir, bool keepOnReload, std::vector<std::string>& loadedFiles, std::vector<std::string>& errors)
{
    fs::path dirPath = dir;

    // 检查目录是否存在且为有效目录
    // 如果目录不存在，不视为错误，直接返回成功
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
        return true;

    // 递归遍历目录中的所有文件
    for (fs::directory_entry const& f : fs::recursive_directory_iterator(dirPath))
    {
        // 跳过非普通文件（如目录、符号链接等）
        if (!fs::is_regular_file(f))
            continue;

        // 获取文件的绝对路径
        fs::path configFile = fs::absolute(f);

        // 只处理 .conf 扩展名的文件
        if (configFile.extension() != ".conf")
            continue;

        std::string fileName = configFile.generic_string();
        std::string error;

        // 尝试加载配置文件
        if (LoadAdditionalFile(fileName, keepOnReload, error))
            loadedFiles.push_back(std::move(fileName));
        else
            errors.push_back(std::move(error));
    }

    // 返回是否所有文件都加载成功
    return errors.empty();
}

/**
 * @brief 使用环境变量覆盖配置值
 *
 * 职责：
 *   检查所有配置项，如果存在对应的环境变量则用环境变量值覆盖配置文件中的值
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   std::vector<std::string> - 被环境变量覆盖的配置键列表
 *
 * 主要流程：
 *   1. 获取配置锁（线程安全）
 *   2. 遍历所有配置项
 *   3. 检查每个键是否存在对应的环境变量（格式：TC_键名）
 *   4. 如果存在环境变量，用其值替换配置值
 *   5. 收集被覆盖的键名并返回
 */
std::vector<std::string> ConfigMgr::OverrideWithEnvVariablesIfAny()
{
    // 获取配置锁，保证线程安全
    std::lock_guard<std::mutex> lock(_configLock);

    std::vector<std::string> overriddenKeys;

    // 遍历所有配置项
    for (bpt::ptree::value_type& itr: _config)
    {
        // 跳过非叶子节点和空键名
        // 空的 second 表示这是一个叶子节点（实际的配置项）
        if (!itr.second.empty() || itr.first.empty())
            continue;

        // 检查是否存在对应的环境变量
        Optional<std::string> envVar = EnvVarForIniKey(itr.first);
        if (!envVar)
            continue;

        // 用环境变量的值替换配置文件中的值
        itr.second = bpt::ptree(*envVar);

        // 记录被覆盖的键名
        overriddenKeys.push_back(itr.first);
    }

    return overriddenKeys;
}

/**
 * @brief 获取配置管理器单例实例
 *
 * 职责：
 *   实现单例模式，返回全局唯一的ConfigMgr实例
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   ConfigMgr* - 配置管理器实例指针
 *
 * 主要流程：
 *   使用静态局部变量实现线程安全的单例模式（C++11 magic statics）
 */
ConfigMgr* ConfigMgr::instance()
{
    // C++11 魔法静态变量：保证线程安全的单例模式
    // 静态局部变量在第一次调用时初始化，后续调用直接返回已创建的实例
    static ConfigMgr instance;
    return &instance;
}

/**
 * @brief 重新加载所有配置文件
 *
 * 职责：
 *   重新加载主配置文件和所有标记为keepOnReload的额外配置文件
 *
 * 参数：
 *   @param errors - 输出参数，加载失败的错误信息列表
 *
 * 返回值：
 *   bool - 所有文件重载成功返回true，有错误返回false
 *
 * 主要流程：
 *   1. 重新加载主配置文件（_filename）
 *   2. 遍历额外配置文件列表，依次重新加载
 *   3. 调用OverrideWithEnvVariablesIfAny应用环境变量覆盖
 *   4. 收集所有错误信息并返回结果
 */
bool ConfigMgr::Reload(std::vector<std::string>& errors)
{
    std::string error;

    // 重新加载主配置文件
    // 注意：LoadInitial 会移动 _args，所以需要使用 std::move(_args)
    if (!LoadInitial(_filename, std::move(_args), error))
        errors.push_back(std::move(error));

    // 重新加载所有标记为 keepOnReload 的额外配置文件
    // 注意：这里传入 false 是因为文件已经在 _additonalFiles 列表中，不需要再次添加
    for (std::string const& additionalFile : _additonalFiles)
        if (!LoadAdditionalFile(additionalFile, false, error))
            errors.push_back(std::move(error));

    // 重新应用环境变量覆盖
    OverrideWithEnvVariablesIfAny();

    return errors.empty();
}

/**
 * @brief 获取配置项值（泛型模板版本）
 *
 * 职责：
 *   从配置树中获取指定键的值，支持类型转换。如果键不存在或转换失败，返回默认值
 *
 * 参数：
 *   @param name  - 配置项键名（支持使用'/'分隔的路径）
 *   @param def   - 默认值（配置项不存在或无效时使用）
 *   @param quiet - 是否静默模式（true=不输出警告日志）
 *
 * 返回值：
 *   T - 配置项值（类型由模板参数决定）
 *
 * 主要流程：
 *   1. 尝试从配置树中获取指定类型的值
 *   2. 如果键不存在（ptree_bad_path）：
 *      a. 检查是否存在对应的环境变量
 *      b. 尝试将环境变量值转换为目标类型
 *      c. 转换失败则使用默认值
 *   3. 如果值类型不匹配（ptree_bad_data），记录错误并使用默认值
 *   4. 非静默模式下输出警告日志提示缺失配置项
 */
template<class T>
T ConfigMgr::GetValueDefault(std::string const& name, T def, bool quiet) const
{
    try
    {
        // 尝试从配置树中获取指定类型的值
        // 使用 '/' 作为路径分隔符，支持嵌套配置项
        return _config.get<T>(bpt::ptree::path_type(name, '/'));
    }
    catch (bpt::ptree_bad_path const&)
    {
        // 配置项不存在：尝试从环境变量中获取
        Optional<std::string> envVar = EnvVarForIniKey(name);
        if (envVar)
        {
            // 尝试将环境变量的字符串值转换为目标类型 T
            Optional<T> castedVar = Trinity::StringTo<T>(*envVar);
            if (!castedVar)
            {
                // 类型转换失败，记录错误并返回默认值
                TC_LOG_ERROR("server.loading", "Bad value defined for name {} in environment variables, going to use default instead", name);
                return def;
            }

            // 成功从环境变量获取值，记录警告（除非静默模式）
            if (!quiet)
                TC_LOG_WARN("server.loading", "Missing name {} in config file {}, recovered with environment '{}' value.", name, _filename, envVar->c_str());

            return *castedVar;
        }
        else if (!quiet)
        {
            // 配置项不存在且无环境变量，记录警告
            TC_LOG_WARN("server.loading", "Missing name {} in config file {}, add \"{} = {}\" to this file",
                name, _filename, name, def);
        }
    }
    catch (bpt::ptree_bad_data const&)
    {
        // 配置项存在但类型不匹配，记录错误并使用默认值
        TC_LOG_ERROR("server.loading", "Bad value defined for name {} in config file {}, going to use {} instead",
            name, _filename, def);
    }

    return def;
}

/**
 * @brief 获取配置项值（std::string特化版本）
 *
 * 职责：
 *   从配置树中获取字符串类型的配置值。字符串类型不需要类型转换
 *
 * 参数：
 *   @param name  - 配置项键名
 *   @param def   - 默认字符串值
 *   @param quiet - 是否静默模式
 *
 * 返回值：
 *   std::string - 配置项字符串值
 *
 * 主要流程：
 *   与通用模板版本类似，但省略了类型转换步骤：
 *   1. 尝试从配置树获取字符串值
 *   2. 键不存在时检查环境变量
 *   3. 值类型不匹配时记录错误
 *   4. 返回配置值或默认值
 */
template<>
std::string ConfigMgr::GetValueDefault<std::string>(std::string const& name, std::string def, bool quiet) const
{
    try
    {
        // 尝试从配置树中获取字符串值
        return _config.get<std::string>(bpt::ptree::path_type(name, '/'));
    }
    catch (bpt::ptree_bad_path const&)
    {
        // 配置项不存在：尝试从环境变量中获取
        Optional<std::string> envVar = EnvVarForIniKey(name);
        if (envVar)
        {
            // 字符串类型不需要类型转换，直接返回环境变量的值
            if (!quiet)
                TC_LOG_WARN("server.loading", "Missing name {} in config file {}, recovered with environment '{}' value.", name, _filename, envVar->c_str());

            return *envVar;
        }
        else if (!quiet)
        {
            // 配置项不存在且无环境变量，记录警告
            TC_LOG_WARN("server.loading", "Missing name {} in config file {}, add \"{} = {}\" to this file",
                name, _filename, name, def);
        }
    }
    catch (bpt::ptree_bad_data const&)
    {
        // 配置项存在但类型不匹配，记录错误并使用默认值
        TC_LOG_ERROR("server.loading", "Bad value defined for name {} in config file {}, going to use {} instead",
            name, _filename, def);
    }

    return def;
}

/**
 * @brief 获取字符串配置项值并移除引号
 *
 * 职责：
 *   获取字符串类型的配置值，并去除值中的所有双引号字符
 *
 * 参数：
 *   @param name  - 配置项键名
 *   @param def   - 默认字符串值
 *   @param quiet - 是否静默模式
 *
 * 返回值：
 *   std::string - 去除引号后的配置字符串值
 *
 * 主要流程：
 *   1. 调用GetValueDefault获取字符串值
 *   2. 使用std::remove移除所有双引号字符
 *   3. 返回处理后的字符串
 */
std::string ConfigMgr::GetStringDefault(std::string const& name, const std::string& def, bool quiet) const
{
    // 调用模板函数获取字符串值
    std::string val = GetValueDefault(name, def, quiet);

    // 移除字符串中的所有双引号字符
    // INI 配置文件中的值可能被引号包围，需要清理
    val.erase(std::remove(val.begin(), val.end(), '"'), val.end());

    return val;
}

/**
 * @brief 获取布尔类型配置项值
 *
 * 职责：
 *   获取布尔类型的配置值，支持多种格式（如"1"/"0"、"true"/"false"）
 *
 * 参数：
 *   @param name  - 配置项键名
 *   @param def   - 默认布尔值
 *   @param quiet - 是否静默模式
 *
 * 返回值：
 *   bool - 配置项布尔值
 *
 * 主要流程：
 *   1. 将默认布尔值转换为字符串（true->"1", false->"0"）
 *   2. 调用GetValueDefault获取字符串值
 *   3. 移除值中的双引号
 *   4. 使用Trinity::StringTo<bool>将字符串转换为布尔值
 *   5. 转换失败时记录错误并返回默认值
 */
bool ConfigMgr::GetBoolDefault(std::string const& name, bool def, bool quiet) const
{
    // 将布尔默认值转换为字符串格式：true -> "1", false -> "0"
    std::string val = GetValueDefault(name, std::string(def ? "1" : "0"), quiet);

    // 移除字符串中的双引号
    val.erase(std::remove(val.begin(), val.end(), '"'), val.end());

    // 尝试将字符串转换为布尔值
    // 支持多种格式：1/0, true/false, on/off, yes/no 等
    Optional<bool> boolVal = Trinity::StringTo<bool>(val);
    if (boolVal)
        return *boolVal;
    else
    {
        // 转换失败，记录错误并返回默认值
        TC_LOG_ERROR("server.loading", "Bad value defined for name {} in config file {}, going to use '{}' instead",
            name, _filename, def ? "true" : "false");
        return def;
    }
}

/**
 * @brief 获取整数类型配置项值
 *
 * 职责：
 *   获取整数类型的配置值
 *
 * 参数：
 *   @param name  - 配置项键名
 *   @param def   - 默认整数值
 *   @param quiet - 是否静默模式
 *
 * 返回值：
 *   int - 配置项整数值
 *
 * 主要流程：
 *   直接调用模板函数GetValueDefault<int>获取值
 */
int ConfigMgr::GetIntDefault(std::string const& name, int def, bool quiet) const
{
    // 直接调用模板函数，利用 Trinity::StringTo<int> 进行类型转换
    return GetValueDefault(name, def, quiet);
}

/**
 * @brief 获取浮点数类型配置项值
 *
 * 职责：
 *   获取浮点数类型的配置值
 *
 * 参数：
 *   @param name  - 配置项键名
 *   @param def   - 默认浮点数值
 *   @param quiet - 是否静默模式
 *
 * 返回值：
 *   float - 配置项浮点数值
 *
 * 主要流程：
 *   直接调用模板函数GetValueDefault<float>获取值
 */
float ConfigMgr::GetFloatDefault(std::string const& name, float def, bool quiet) const
{
    // 直接调用模板函数，利用 Trinity::StringTo<float> 进行类型转换
    return GetValueDefault(name, def, quiet);
}

/**
 * @brief 获取主配置文件名
 *
 * 职责：
 *   返回当前加载的主配置文件的完整路径
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   std::string const& - 配置文件路径的常量引用
 *
 * 主要流程：
 *   1. 获取配置锁（线程安全）
 *   2. 返回_filename的常量引用
 */
std::string const& ConfigMgr::GetFilename()
{
    std::lock_guard<std::mutex> lock(_configLock);
    return _filename;
}

/**
 * @brief 获取命令行参数列表
 *
 * 职责：
 *   返回程序启动时传入的命令行参数列表
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   std::vector<std::string> const& - 命令行参数列表的常量引用
 *
 * 主要流程：
 *   直接返回_args的常量引用
 */
std::vector<std::string> const& ConfigMgr::GetArguments() const
{
    return _args;
}

/**
 * @brief 获取以指定前缀开头的所有配置键名
 *
 * 职责：
 *   搜索配置树中所有以指定字符串开头的配置键，用于批量获取相关配置项
 *
 * 参数：
 *   @param name - 键名前缀字符串
 *
 * 返回值：
 *   std::vector<std::string> - 匹配的配置键名列表
 *
 * 主要流程：
 *   1. 获取配置锁（线程安全）
 *   2. 创建空的结果列表
 *   3. 遍历所有配置项
 *   4. 检查键名是否以指定前缀开头
 *   5. 将匹配的键名添加到结果列表
 *   6. 返回结果列表
 */
std::vector<std::string> ConfigMgr::GetKeysByString(std::string const& name)
{
    std::lock_guard<std::mutex> lock(_configLock);

    std::vector<std::string> keys;

    for (bpt::ptree::value_type const& child : _config)
        if (child.first.compare(0, name.length(), name) == 0)
            keys.push_back(child.first);

    return keys;
}
