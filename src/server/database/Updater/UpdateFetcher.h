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
 * @file UpdateFetcher.h
 * @brief 数据库更新获取器模块
 *
 * 本模块负责管理数据库 SQL 更新文件的检测、验证和应用流程。
 * 主要功能包括：
 * - 扫描文件系统中的 SQL 更新文件
 * - 检查已应用的更新记录
 * - 比对文件哈希值检测变更
 * - 自动应用新的或已变更的更新
 * - 处理文件重命名和状态更新
 * - 清理孤立的数据库记录
 *
 * 该模块是数据库版本控制系统（DBUpdater）的核心组件，
 * 确保数据库架构与代码版本保持同步。
 */

#ifndef UpdateFetcher_h__
#define UpdateFetcher_h__

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace boost
{
    namespace filesystem
    {
        class path;
    }
}

/**
 * @struct UpdateResult
 * @brief 数据库更新操作的结果统计
 *
 * 用于存储更新操作执行后的统计信息，包括已更新、最新和归档的更新数量。
 * 该结构体在 UpdateFetcher::Update() 方法执行完毕后返回。
 */
struct TC_DATABASE_API UpdateResult
{
    /**
     * @brief 默认构造函数，初始化所有计数为 0
     */
    UpdateResult()
        : updated(0), recent(0), archived(0) { }

    /**
     * @brief 带参数的构造函数
     * @param updated_ 本次新应用的更新数量
     * @param recent_ 当前发布状态的更新数量
     * @param archived_ 已归档状态的更新数量
     */
    UpdateResult(size_t const updated_, size_t const recent_, size_t const archived_)
        : updated(updated_), recent(recent_), archived(archived_) { }

    size_t updated;   ///< 本次执行中成功应用的更新数量
    size_t recent;    ///< 数据库中处于 RELEASED 状态的更新数量
    size_t archived;  ///< 数据库中处于 ARCHIVED 状态的更新数量
};

/**
 * @class UpdateFetcher
 * @brief 数据库 SQL 更新文件的获取和验证处理器
 *
 * 该类负责从文件系统和数据库中获取 SQL 更新信息，并执行更新验证、应用和管理操作。
 *
 * 核心职责：
 * - 扫描指定目录及其子目录中的 SQL 更新文件
 * - 从数据库读取已应用的更新记录
 * - 通过 SHA1 哈希值比对检测文件变更
 * - 自动应用新的更新或重新应用已变更的更新
 * - 处理更新文件的重命名操作
 * - 清理孤立的数据库记录（源文件已被删除的记录）
 *
 * 工作流程：
 * 1. GetFileList() 获取文件系统中的所有可用更新文件
 * 2. ReceiveAppliedFiles() 获取数据库中已应用的更新记录
 * 3. Update() 比对并应用更新
 *
 * 线程安全：
 * 该类不保证线程安全，应在单线程环境中使用。
 *
 * 性能考虑：
 * - 文件哈希计算可能耗时，建议在服务器启动时执行
 * - 递归扫描目录有最大深度限制（10层）以防止无限递归
 */
class TC_DATABASE_API UpdateFetcher
{
    typedef boost::filesystem::path Path;

public:
    /**
     * @brief 构造函数，初始化更新获取器
     * @param updateDirectory 更新文件的源目录路径
     * @param apply 应用 SQL 语句的回调函数，用于执行更新操作
     * @param applyFile 应用整个 SQL 文件的回调函数
     * @param retrieve 查询数据库的回调函数，用于获取已应用的更新记录
     *
     * 这三个回调函数由调用者提供，使该类可以与不同的数据库实现解耦。
     */
    UpdateFetcher(Path const& updateDirectory,
        std::function<void(std::string const&)> const& apply,
        std::function<void(Path const& path)> const& applyFile,
        std::function<QueryResult(std::string const&)> const& retrieve);

    /**
     * @brief 析构函数
     */
    ~UpdateFetcher();

    /**
     * @brief 执行数据库更新检查和应用操作
     * @param redundancyChecks 是否执行冗余检查（检查已应用更新的哈希值是否变化）
     * @param allowRehash 是否允许重新计算空哈希值的更新
     * @param archivedRedundancy 是否对归档文件执行冗余检查
     * @param cleanDeadReferencesMaxCount 清理孤立记录的最大数量阈值，-1 表示无限制
     * @return UpdateResult 更新操作的统计结果
     *
     * 该方法是更新流程的核心，执行以下操作：
     * 1. 获取所有可用的更新文件
     * 2. 获取数据库中已应用的更新记录
     * 3. 遍历所有可用更新，比对哈希值
     * 4. 应用新的更新或重新应用已变更的更新
     * 5. 清理孤立的数据库记录
     *
     * 调用时机：
     * - 服务器启动时的数据库初始化阶段
     * - 数据库版本检查和升级操作
     *
     * 性能注意事项：
     * - 当启用冗余检查时，会对所有已应用的更新计算哈希值
     * - 大量更新文件可能增加启动时间
     */
    UpdateResult Update(bool const redundancyChecks, bool const allowRehash,
                  bool const archivedRedundancy, int32 const cleanDeadReferencesMaxCount) const;

private:
    /**
     * @enum UpdateMode
     * @brief 更新操作的模式
     */
    enum UpdateMode
    {
        MODE_APPLY,   ///< 应用模式：执行 SQL 更新并记录
        MODE_REHASH   ///< 重哈希模式：仅更新记录中的哈希值，不执行 SQL
    };

    /**
     * @enum State
     * @brief 更新文件的状态
     */
    enum State
    {
        RELEASED,   ///< 已发布状态：当前活跃的更新文件
        ARCHIVED    ///< 已归档状态：历史归档的更新文件，不再变更
    };

    /**
     * @struct AppliedFileEntry
     * @brief 已应用的更新文件记录
     *
     * 该结构体表示数据库 updates 表中的一条记录，
     * 用于跟踪每个已应用的 SQL 更新文件的元数据信息。
     */
    struct AppliedFileEntry
    {
        /**
         * @brief 构造函数
         * @param name_ 更新文件的名称（不含路径）
         * @param hash_ 文件内容的 SHA1 哈希值（十六进制字符串）
         * @param state_ 更新状态（RELEASED 或 ARCHIVED）
         * @param timestamp_ 更新应用的时间戳（Unix 时间戳）
         */
        AppliedFileEntry(std::string const& name_, std::string const& hash_, State state_, uint64 timestamp_)
            : name(name_), hash(hash_), state(state_), timestamp(timestamp_) { }

        std::string const name;       ///< 更新文件名（仅文件名，不含路径）
        std::string const hash;       ///< 文件内容的 SHA1 哈希值，用于检测文件变更
        State const state;            ///< 更新状态：RELEASED（已发布）或 ARCHIVED（已归档）
        uint64 const timestamp;       ///< 更新被应用的时间戳（Unix 时间戳，秒）

        /**
         * @brief 将字符串状态转换为枚举状态
         * @param state 状态字符串（"RELEASED" 或其他）
         * @return State 对应的枚举值
         */
        static inline State StateConvert(std::string const& state)
        {
            return (state == "RELEASED") ? RELEASED : ARCHIVED;
        }

        /**
         * @brief 将枚举状态转换为字符串状态
         * @param state 枚举状态值
         * @return std::string 状态字符串（"RELEASED" 或 "ARCHIVED"）
         */
        static inline std::string StateConvert(State const state)
        {
            return (state == RELEASED) ? "RELEASED" : "ARCHIVED";
        }

        /**
         * @brief 获取状态的字符串表示
         * @return std::string 当前状态的字符串形式
         */
        std::string GetStateAsString() const
        {
            return StateConvert(state);
        }
    };

    struct DirectoryEntry;   ///< 目录条目结构体，定义在 cpp 文件中

    typedef std::pair<Path, State> LocaleFileEntry;   ///< 本地文件条目：包含路径和状态的键值对

    /**
     * @struct PathCompare
     * @brief 文件路径比较器，用于 LocaleFileStorage 集合的排序
     *
     * 该比较器仅比较文件名（不含路径），确保集合中的文件名唯一。
     */
    struct PathCompare
    {
        /**
         * @brief 比较两个本地文件条目
         * @param left 左侧文件条目
         * @param right 右侧文件条目
         * @return bool 如果 left 的文件名小于 right 的文件名则返回 true
         */
        bool operator()(LocaleFileEntry const& left, LocaleFileEntry const& right) const;
    };

    typedef std::set<LocaleFileEntry, PathCompare> LocaleFileStorage;   ///< 本地文件存储集合，按文件名排序且唯一
    typedef std::unordered_map<std::string, std::string> HashToFileNameStorage;   ///< 哈希值到文件名的映射，用于检测重命名
    typedef std::unordered_map<std::string, AppliedFileEntry> AppliedFileStorage;   ///< 已应用文件的映射，键为文件名
    typedef std::vector<UpdateFetcher::DirectoryEntry> DirectoryStorage;   ///< 目录条目列表

    /**
     * @brief 获取所有可用的更新文件列表
     * @return LocaleFileStorage 包含所有可用更新文件的集合
     *
     * 该方法从数据库读取需要包含的目录列表，
     * 然后递归扫描这些目录中的所有 .sql 文件。
     */
    LocaleFileStorage GetFileList() const;

    /**
     * @brief 递归填充文件列表
     * @param path 当前扫描的目录路径
     * @param storage 输出的文件存储集合
     * @param state 当前目录的状态（RELEASED 或 ARCHIVED）
     * @param depth 当前递归深度，用于防止无限递归
     *
     * 递归扫描指定目录及其子目录，收集所有 .sql 文件。
     * 最大递归深度限制为 10 层以防止符号链接导致的无限循环。
     *
     * 性能注意事项：
     * - 大量子目录可能增加扫描时间
     * - 建议避免过深的目录嵌套结构
     */
    void FillFileListRecursively(Path const& path, LocaleFileStorage& storage,
        State const state, uint32 const depth) const;

    /**
     * @brief 从数据库获取需要包含的目录列表
     * @return DirectoryStorage 目录条目列表
     *
     * 查询 updates_include 表获取配置的更新目录。
     * 路径中的 "$" 前缀会被替换为源目录路径。
     */
    DirectoryStorage ReceiveIncludedDirectories() const;

    /**
     * @brief 从数据库获取已应用的更新文件记录
     * @return AppliedFileStorage 已应用文件的映射表，键为文件名
     *
     * 查询 updates 表获取所有已应用的更新记录。
     * 这些记录用于后续的变更检测和冗余检查。
     */
    AppliedFileStorage ReceiveAppliedFiles() const;

    /**
     * @brief 读取 SQL 更新文件的内容
     * @param file SQL 文件的路径
     * @return std::string 文件的完整内容
     *
     * 将整个 SQL 文件读入字符串，用于计算哈希值和执行更新。
     *
     * 异常处理：
     * - 如果文件无法打开，抛出 UpdateException 异常
     */
    std::string ReadSQLUpdate(Path const& file) const;

    /**
     * @brief 应用指定的 SQL 更新文件
     * @param path SQL 文件的路径
     * @return uint32 执行耗时（毫秒）
     *
     * 执行 SQL 文件并测量执行时间，用于性能监控。
     */
    uint32 Apply(Path const& path) const;

    /**
     * @brief 更新或插入更新记录到数据库
     * @param entry 更新文件条目信息
     * @param speed 执行耗时（毫秒），默认为 0
     *
     * 使用 REPLACE INTO 语句更新 updates 表中的记录。
     * 如果记录已存在则更新，否则插入新记录。
     */
    void UpdateEntry(AppliedFileEntry const& entry, uint32 const speed = 0) const;

    /**
     * @brief 重命名更新记录
     * @param from 原文件名
     * @param to 新文件名
     *
     * 当检测到文件重命名时调用此方法。
     * 首先删除目标文件名的记录（如果存在），
     * 然后更新原文件名为新文件名。
     */
    void RenameEntry(std::string const& from, std::string const& to) const;

    /**
     * @brief 清理孤立的数据库记录
     * @param storage 需要清理的记录映射表
     *
     * 删除数据库中存在但源文件已不存在的更新记录。
     * 这些通常是已被移除的旧更新文件。
     */
    void CleanUp(AppliedFileStorage const& storage) const;

    /**
     * @brief 更新记录的状态
     * @param name 文件名
     * @param state 新状态
     *
     * 当文件移动到不同状态的目录时更新其状态。
     */
    void UpdateState(std::string const& name, State const state) const;

    std::unique_ptr<Path> const _sourceDirectory;   ///< 更新文件的源目录路径

    std::function<void(std::string const&)> const _apply;   ///< 执行 SQL 语句的回调函数
    std::function<void(Path const& path)> const _applyFile;   ///< 执行 SQL 文件的回调函数
    std::function<QueryResult(std::string const&)> const _retrieve;   ///< 查询数据库的回调函数
};

#endif // UpdateFetcher_h__
