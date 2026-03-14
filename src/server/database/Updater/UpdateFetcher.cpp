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
 * @file UpdateFetcher.cpp
 * @brief 数据库更新获取器的实现文件
 *
 * 本文件实现了 UpdateFetcher 类的所有方法，包括：
 * - 文件系统扫描和文件列表生成
 * - 数据库记录的查询和更新
 * - SQL 更新的应用和验证
 * - 更新记录的重命名、清理和状态管理
 *
 * 关键实现细节：
 * - 使用 SHA1 哈希值检测文件变更
 * - 通过回调函数实现与具体数据库实现的解耦
 * - 支持归档文件和已发布文件的状态区分
 * - 自动处理文件重命名和孤立记录清理
 */

#include "UpdateFetcher.h"
#include "CryptoHash.h"
#include "DBUpdater.h"
#include "Field.h"
#include "Log.h"
#include "QueryResult.h"
#include "Util.h"
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <fstream>
#include <sstream>

using namespace boost::filesystem;

/**
 * @struct UpdateFetcher::DirectoryEntry
 * @brief 目录条目结构体
 *
 * 用于存储从 updates_include 表读取的目录信息，
 * 包含目录路径和该目录中文件的状态。
 */
struct UpdateFetcher::DirectoryEntry
{
    /**
     * @brief 构造函数
     * @param path_ 目录路径
     * @param state_ 该目录中文件的状态（RELEASED 或 ARCHIVED）
     */
    DirectoryEntry(Path const& path_, State state_) : path(path_), state(state_) { }

    Path const path;     ///< 目录路径
    State const state;   ///< 该目录中文件的默认状态
};

/**
 * @brief 构造函数实现
 *
 * 初始化更新获取器的成员变量，包括源目录路径和三个回调函数。
 * 回调函数的设计使该类可以与不同的数据库实现解耦。
 */
UpdateFetcher::UpdateFetcher(Path const& sourceDirectory,
    std::function<void(std::string const&)> const& apply,
    std::function<void(Path const& path)> const& applyFile,
    std::function<QueryResult(std::string const&)> const& retrieve) :
        _sourceDirectory(std::make_unique<Path>(sourceDirectory)), _apply(apply), _applyFile(applyFile),
        _retrieve(retrieve)
{
}

/**
 * @brief 析构函数实现
 *
 * 默认析构函数，unique_ptr 成员会自动释放内存。
 */
UpdateFetcher::~UpdateFetcher()
{
}

/**
 * @brief 获取所有可用的更新文件列表
 * @return LocaleFileStorage 包含所有可用 SQL 更新文件的集合
 *
 * 工作流程：
 * 1. 从数据库读取需要包含的目录列表（updates_include 表）
 * 2. 对每个目录递归扫描其中的 .sql 文件
 * 3. 返回包含所有文件的集合，按文件名排序且唯一
 */
UpdateFetcher::LocaleFileStorage UpdateFetcher::GetFileList() const
{
    LocaleFileStorage files;
    DirectoryStorage directories = ReceiveIncludedDirectories();
    // 遍历所有配置的目录，递归扫描其中的 SQL 文件
    for (auto const& entry : directories)
        FillFileListRecursively(entry.path, files, entry.state, 1);

    return files;
}

/**
 * @brief 递归填充文件列表
 * @param path 当前扫描的目录路径
 * @param storage 输出的文件存储集合
 * @param state 当前目录中文件的默认状态
 * @param depth 当前递归深度
 *
 * 递归扫描目录树，收集所有 .sql 文件。
 * 实现细节：
 * - 最大深度限制为 10 层，防止符号链接导致的无限循环
 * - 只处理 .sql 扩展名的文件
 * - 检测重复文件名，确保每个文件名唯一
 */
void UpdateFetcher::FillFileListRecursively(Path const& path, LocaleFileStorage& storage, State const state, uint32 const depth) const
{
    // 最大递归深度限制，防止符号链接导致的无限循环
    static uint32 const MAX_DEPTH = 10;
    // 目录迭代器的结束标记
    static directory_iterator const end;

    // 遍历当前目录中的所有条目
    for (directory_iterator itr(path); itr != end; ++itr)
    {
        // 如果是目录且未超过最大深度，继续递归
        if (is_directory(itr->path()))
        {
            if (depth < MAX_DEPTH)
                FillFileListRecursively(itr->path(), storage, state, depth + 1);
        }
        // 如果是 .sql 文件，添加到存储中
        else if (itr->path().extension() == ".sql")
        {
            TC_LOG_TRACE("sql.updates", "Added locale file \"{}\".", itr->path().filename().generic_string());

            LocaleFileEntry const entry = { itr->path(), state };

            // 检查重复的文件名
            // 由于集合按文件名排序比较，这样可以确保文件名唯一
            if (storage.find(entry) != storage.end())
            {
                TC_LOG_FATAL("sql.updates", "Duplicate filename \"{}\" occurred. Because updates are ordered " \
                    "by their filenames, every name needs to be unique!", itr->path().generic_string());

                throw UpdateException("Updating failed, see the log for details.");
            }

            storage.insert(entry);
        }
    }
}

/**
 * @brief 从数据库获取需要包含的目录列表
 * @return DirectoryStorage 目录条目列表
 *
 * 查询 updates_include 表获取配置的更新目录。
 * 实现细节：
 * - 路径以 "$" 开头时，会被替换为源目录路径
 * - 不存在的目录会被跳过并记录警告日志
 * - 每个目录关联一个状态（RELEASED 或 ARCHIVED）
 */
UpdateFetcher::DirectoryStorage UpdateFetcher::ReceiveIncludedDirectories() const
{
    DirectoryStorage directories;

    // 查询数据库获取需要扫描的目录列表
    QueryResult const result = _retrieve("SELECT `path`, `state` FROM `updates_include`");
    if (!result)
        return directories;

    do
    {
        Field* fields = result->Fetch();

        std::string path = fields[0].GetString();
        // 处理 "$" 前缀，将其替换为源目录路径
        if (path.substr(0, 1) == "$")
            path = _sourceDirectory->generic_string() + path.substr(1);

        Path const p(path);

        // 检查目录是否存在，跳过不存在的目录
        if (!is_directory(p))
        {
            TC_LOG_WARN("sql.updates", "DBUpdater: Given update include directory \"{}\" does not exist, skipped!", p.generic_string());
            continue;
        }

        // 创建目录条目并添加到列表
        DirectoryEntry const entry = { p, AppliedFileEntry::StateConvert(fields[1].GetString()) };
        directories.push_back(entry);

        TC_LOG_TRACE("sql.updates", "Added applied file \"{}\" from remote.", p.filename().generic_string());

    } while (result->NextRow());

    return directories;
}

/**
 * @brief 从数据库获取已应用的更新文件记录
 * @return AppliedFileStorage 已应用文件的映射表，键为文件名
 *
 * 查询 updates 表获取所有已应用的更新记录。
 * 返回的映射表用于后续的变更检测和冗余检查。
 */
UpdateFetcher::AppliedFileStorage UpdateFetcher::ReceiveAppliedFiles() const
{
    AppliedFileStorage map;

    // 查询所有已应用的更新记录，按名称排序
    QueryResult result = _retrieve("SELECT `name`, `hash`, `state`, UNIX_TIMESTAMP(`timestamp`) FROM `updates` ORDER BY `name` ASC");
    if (!result)
        return map;

    do
    {
        Field* fields = result->Fetch();

        // 构建已应用文件条目
        AppliedFileEntry const entry = { fields[0].GetString(), fields[1].GetString(),
            AppliedFileEntry::StateConvert(fields[2].GetString()), fields[3].GetUInt64() };

        map.insert(std::make_pair(entry.name, entry));
    }
    while (result->NextRow());

    return map;
}

/**
 * @brief 读取 SQL 更新文件的内容
 * @param file SQL 文件的路径
 * @return std::string 文件的完整内容
 *
 * 将整个 SQL 文件读入字符串，用于计算哈希值和执行更新。
 *
 * 异常处理：
 * - 如果文件无法打开，抛出 UpdateException 异常
 * - 为保证数据库完整性，服务器会停止运行
 */
std::string UpdateFetcher::ReadSQLUpdate(boost::filesystem::path const& file) const
{
    std::ifstream in(file.c_str());
    if (!in.is_open())
    {
        // 文件打开失败，停止服务器以保持数据库完整性
        TC_LOG_FATAL("sql.updates", "Failed to open the sql update \"{}\" for reading! "
                     "Stopping the server to keep the database integrity, "
                     "try to identify and solve the issue or disable the database updater.",
                     file.generic_string());

        throw UpdateException("Opening the sql update failed!");
    }

    // 使用 lambda 函数读取整个文件内容
    auto update = [&in]  {
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();

    in.close();
    return update;
}

/**
 * @brief 执行数据库更新检查和应用操作（核心方法）
 * @param redundancyChecks 是否执行冗余检查
 * @param allowRehash 是否允许重新计算空哈希值
 * @param archivedRedundancy 是否对归档文件执行冗余检查
 * @param cleanDeadReferencesMaxCount 清理孤立记录的最大数量阈值
 * @return UpdateResult 更新操作的统计结果
 *
 * 这是 UpdateFetcher 类的核心方法，实现了完整的数据库更新流程。
 *
 * 主要流程：
 * 1. 获取所有可用的更新文件列表
 * 2. 获取数据库中已应用的更新记录
 * 3. 构建哈希值到文件名的映射（用于检测重命名）
 * 4. 遍历所有可用更新文件：
 *    a. 检查是否已应用
 *    b. 计算文件哈希值
 *    c. 检测文件重命名、变更或状态变化
 *    d. 应用新的或已变更的更新
 * 5. 清理孤立的数据库记录
 *
 * 关键算法：
 * - 使用 SHA1 哈希值检测文件内容变更
 * - 通过哈希值映射检测文件重命名
 * - 支持归档文件的优化处理
 *
 * 性能考虑：
 * - 每个文件都需要读取并计算哈希值
 * - 大量更新文件可能增加启动时间
 * - 建议在服务器启动时执行
 */
UpdateResult UpdateFetcher::Update(bool const redundancyChecks,
                                   bool const allowRehash,
                                   bool const archivedRedundancy,
                                   int32 const cleanDeadReferencesMaxCount) const
{
    // 获取所有可用的更新文件
    LocaleFileStorage const available = GetFileList();
    // 获取数据库中已应用的更新记录
    AppliedFileStorage applied = ReceiveAppliedFiles();

    size_t countRecentUpdates = 0;
    size_t countArchivedUpdates = 0;

    // 统计已应用更新中发布状态和归档状态的数量
    for (auto const& entry : applied)
        if (entry.second.state == RELEASED)
            ++countRecentUpdates;
        else
            ++countArchivedUpdates;

    // 构建哈希值到文件名的映射缓存，用于检测文件重命名
    HashToFileNameStorage hashToName;
    for (auto entry : applied)
        hashToName.insert(std::make_pair(entry.second.hash, entry.first));

    size_t importedUpdates = 0;

    // 遍历所有可用的更新文件
    for (auto const& availableQuery : available)
    {
        TC_LOG_DEBUG("sql.updates", "Checking update \"{}\"...", availableQuery.first.filename().generic_string());

        // 在已应用列表中查找当前文件
        AppliedFileStorage::const_iterator iter = applied.find(availableQuery.first.filename().string());
        if (iter != applied.end())
        {
            // 如果禁用冗余检查，跳过已应用的更新
            if (!redundancyChecks)
            {
                TC_LOG_DEBUG("sql.updates", ">> Update is already applied, skipping redundancy checks.");
                applied.erase(iter);
                continue;
            }

            // 如果更新在归档目录中且数据库中标记为归档，跳过冗余检查（归档更新不会变更）
            if (!archivedRedundancy && (iter->second.state == ARCHIVED) && (availableQuery.second == ARCHIVED))
            {
                TC_LOG_DEBUG("sql.updates", ">> Update is archived and marked as archived in database, skipping redundancy checks.");
                applied.erase(iter);
                continue;
            }
        }

        // 计算文件内容的 SHA1 哈希值
        std::string const hash = ByteArrayToHexStr(Trinity::Crypto::SHA1::GetDigestOf(ReadSQLUpdate(availableQuery.first)));

        UpdateMode mode = MODE_APPLY;

        // 情况1：更新不在已应用列表中（新文件或重命名文件）
        if (iter == applied.end())
        {
            // 检测文件重命名（不同文件名但相同哈希值）
            HashToFileNameStorage::const_iterator const hashIter = hashToName.find(hash);
            if (hashIter != hashToName.end())
            {
                // 检查原始文件是否还存在。如果存在，则有冲突
                LocaleFileStorage::const_iterator localeIter;
                // 查找原始文件是否在可用列表中
                for (localeIter = available.begin(); (localeIter != available.end()) &&
                    (localeIter->first.filename().string() != hashIter->second); ++localeIter);

                // 冲突：原始文件仍然存在
                if (localeIter != available.end())
                {
                    TC_LOG_WARN("sql.updates", ">> It seems like the update \"{}\" \'{}\' was renamed, but the old file is still there! " \
                        "Treating it as a new file! (It is probably an unmodified copy of the file \"{}\")",
                            availableQuery.first.filename().string(), hash.substr(0, 7),
                                localeIter->first.filename().string());
                }
                // 原始文件已被移除，可以安全地将其视为重命名
                else
                {
                    TC_LOG_INFO("sql.updates", ">> Renaming update \"{}\" to \"{}\" \'{}\'.",
                        hashIter->second, availableQuery.first.filename().string(), hash.substr(0, 7));

                    // 更新数据库中的文件名记录
                    RenameEntry(hashIter->second, availableQuery.first.filename().string());
                    applied.erase(hashIter->second);
                    continue;
                }
            }
            // 全新文件，从未应用过
            else
            {
                TC_LOG_INFO("sql.updates", ">> Applying update \"{}\" \'{}\'...",
                    availableQuery.first.filename().string(), hash.substr(0, 7));
            }
        }
        // 情况2：更新已存在但哈希值为空，需要重新计算哈希
        else if (allowRehash && iter->second.hash.empty())
        {
            mode = MODE_REHASH;

            TC_LOG_INFO("sql.updates", ">> Re-hashing update \"{}\" \'{}\'...", availableQuery.first.filename().string(),
                hash.substr(0, 7));
        }
        // 情况3：更新已存在，检查是否变更
        else
        {
            // 文件内容变更，需要重新应用
            if (iter->second.hash != hash)
            {
                TC_LOG_INFO("sql.updates", ">> Reapplying update \"{}\" \'{}\' -> \'{}\' (it changed)...", availableQuery.first.filename().string(),
                    iter->second.hash.substr(0, 7), hash.substr(0, 7));
            }
            else
            {
                // 文件未变更，仅检查状态是否需要更新
                if (iter->second.state != availableQuery.second)
                {
                    TC_LOG_DEBUG("sql.updates", ">> Updating the state of \"{}\" to \'{}\'...",
                        availableQuery.first.filename().string(), AppliedFileEntry::StateConvert(availableQuery.second));

                    UpdateState(availableQuery.first.filename().string(), availableQuery.second);
                }

                TC_LOG_DEBUG("sql.updates", ">> Update is already applied and matches the hash \'{}\'.", hash.substr(0, 7));

                applied.erase(iter);
                continue;
            }
        }

        // 执行更新操作
        uint32 speed = 0;
        AppliedFileEntry const file = { availableQuery.first.filename().string(), hash, availableQuery.second, 0 };

        switch (mode)
        {
            case MODE_APPLY:
                // 应用 SQL 更新文件
                speed = Apply(availableQuery.first);
                [[fallthrough]];  // 继续执行更新记录
            case MODE_REHASH:
                // 更新数据库中的记录
                UpdateEntry(file, speed);
                break;
        }

        // 从待处理列表中移除已处理的记录
        if (iter != applied.end())
            applied.erase(iter);

        // 统计本次应用的更新数量
        if (mode == MODE_APPLY)
            ++importedUpdates;
    }

    // 清理孤立的数据库记录（如果启用）
    if (!applied.empty())
    {
        // 判断是否执行清理：-1 表示无限制，否则检查数量阈值
        bool const doCleanup = (cleanDeadReferencesMaxCount < 0) || (applied.size() <= static_cast<size_t>(cleanDeadReferencesMaxCount));

        for (auto const& entry : applied)
        {
            // 记录警告：文件曾应用过但现在缺失
            TC_LOG_WARN("sql.updates", ">> The file \'{}\' was applied to the database, but is missing in" \
                " your update directory now!", entry.first);

            if (doCleanup)
                TC_LOG_INFO("sql.updates", "Deleting orphaned entry \'{}\'...", entry.first);
        }

        // 执行清理操作
        if (doCleanup)
            CleanUp(applied);
        else
        {
            // 清理被禁用，记录错误信息
            TC_LOG_ERROR("sql.updates", "Cleanup is disabled! There were  {} dirty files applied to your database, " \
                "but they are now missing in your source directory!", applied.size());
        }
    }

    return UpdateResult(importedUpdates, countRecentUpdates, countArchivedUpdates);
}

/**
 * @brief 应用指定的 SQL 更新文件
 * @param path SQL 文件的路径
 * @return uint32 执行耗时（毫秒）
 *
 * 执行 SQL 文件并测量执行时间。
 * 使用高精度时钟进行性能基准测试。
 */
uint32 UpdateFetcher::Apply(Path const& path) const
{
    using Time = std::chrono::high_resolution_clock;

    // 记录开始时间
    auto const begin = Time::now();

    // 执行 SQL 文件
    _applyFile(path);

    // 计算并返回执行耗时（毫秒）
    return uint32(std::chrono::duration_cast<std::chrono::milliseconds>(Time::now() - begin).count());
}

/**
 * @brief 更新或插入更新记录到数据库
 * @param entry 更新文件条目信息
 * @param speed 执行耗时（毫秒）
 *
 * 使用 REPLACE INTO 语句更新 updates 表。
 * 如果记录已存在则更新，否则插入新记录。
 */
void UpdateFetcher::UpdateEntry(AppliedFileEntry const& entry, uint32 const speed) const
{
    // 构建 REPLACE INTO SQL 语句
    std::string const update = "REPLACE INTO `updates` (`name`, `hash`, `state`, `speed`) VALUES (\"" +
        entry.name + "\", \"" + entry.hash + "\", \'" + entry.GetStateAsString() + "\', " + std::to_string(speed) + ")";

    // 执行 SQL 语句
    _apply(update);
}

/**
 * @brief 重命名更新记录
 * @param from 原文件名
 * @param to 新文件名
 *
 * 当检测到文件重命名时调用。
 * 执行步骤：
 * 1. 删除目标文件名的记录（如果存在）
 * 2. 更新原文件名为新文件名
 */
void UpdateFetcher::RenameEntry(std::string const& from, std::string const& to) const
{
    // 第一步：删除目标文件名的记录（如果存在）
    {
        std::string const update = "DELETE FROM `updates` WHERE `name`=\"" + to + "\"";

        _apply(update);
    }

    // 第二步：将原文件名更新为新文件名
    {
        std::string const update = "UPDATE `updates` SET `name`=\"" + to + "\" WHERE `name`=\"" + from + "\"";

        _apply(update);
    }
}

/**
 * @brief 清理孤立的数据库记录
 * @param storage 需要清理的记录映射表
 *
 * 删除数据库中存在但源文件已不存在的更新记录。
 * 使用批量 DELETE 语句提高效率，一次性删除所有孤立记录。
 *
 * SQL 示例：
 * DELETE FROM `updates` WHERE `name` IN("file1.sql", "file2.sql", ...)
 */
void UpdateFetcher::CleanUp(AppliedFileStorage const& storage) const
{
    if (storage.empty())
        return;

    // 构建批量删除语句
    std::stringstream update;
    size_t remaining = storage.size();

    update << "DELETE FROM `updates` WHERE `name` IN(";

    // 添加所有需要删除的文件名
    for (auto const& entry : storage)
    {
        update << "\"" << entry.first << "\"";
        if ((--remaining) > 0)
            update << ", ";
    }

    update << ")";

    // 执行删除操作
    _apply(update.str());
}

/**
 * @brief 更新记录的状态
 * @param name 文件名
 * @param state 新状态
 *
 * 当文件移动到不同状态的目录时调用此方法更新其状态。
 * 例如：从 RELEASED 目录移动到 ARCHIVED 目录。
 */
void UpdateFetcher::UpdateState(std::string const& name, State const state) const
{
    // 构建 UPDATE SQL 语句
    std::string const update = "UPDATE `updates` SET `state`=\'" + AppliedFileEntry::StateConvert(state) + "\' WHERE `name`=\"" + name + "\"";

    // 执行更新操作
    _apply(update);
}

/**
 * @brief 比较两个本地文件条目的文件名
 * @param left 左侧文件条目
 * @param right 右侧文件条目
 * @return bool 如果 left 的文件名小于 right 的文件名则返回 true
 *
 * 该比较器仅比较文件名（不含路径），确保集合中的文件名唯一。
 * 这是 LocaleFileStorage 集合排序和唯一性保证的基础。
 */
bool UpdateFetcher::PathCompare::operator()(LocaleFileEntry const& left, LocaleFileEntry const& right) const
{
    return left.first.filename().string() < right.first.filename().string();
}
