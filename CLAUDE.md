# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

TrinityCore 是一个基于 C++ 的 MMORPG 框架,源自 MaNGOS 项目。当前分支为 3.3.5 (Wrath of the Lich King 版本)。

## 构建命令

### 标准构建流程

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目 (默认使用 RelWithDebInfo)
cmake ../ -DCMAKE_INSTALL_PREFIX=/path/to/install -DTOOLS=1 -DWITH_WARNINGS=1

# 编译 (使用多核加速)
make -j4

# 安装
make install

# 运行测试 (需要启用 BUILD_TESTING)
cmake ../ -DBUILD_TESTING=1
make -j4
make test
```

### 常用 CMake 选项

- `SERVERS`: 构建 worldserver 和 authserver (默认 ON)
- `SCRIPTS`: 脚本编译模式: `none`, `static`, `dynamic`, `minimal-static`, `minimal-dynamic` (默认 static)
- `TOOLS`: 构建地图/移动地图提取和汇编工具 (默认 ON)
- `USE_COREPCH`/`USE_SCRIPTPCH`: 使用预编译头 (默认 ON)
- `WITH_WARNINGS`: 显示所有编译警告 (推荐启用)
- `WITH_COREDEBUG`: 包含额外调试代码
- `BUILD_TESTING`: 构建单元测试 (默认 OFF)
- `CMAKE_BUILD_TYPE`: 构建类型 - `Release`, `MinSizeRel`, `RelWithDebInfo`, `Debug`

### Debug 构建示例

```bash
cmake ../ -DCMAKE_BUILD_TYPE=Debug -DWITH_COREDEBUG=1 -DBUILD_TESTING=1
make -j4
```

## 代码风格检查

运行代码风格检查脚本:

```bash
./contrib/check_codestyle.sh
```

主要规则:
- 使用 4 个空格代替制表符
- 行尾不得有空格
- 日志中使用 `ObjectGuid::ToString().c_str()` 而非 `GetCounter()`
- 避免多个连续空行

## 项目架构

### 核心目录结构

- `src/common/`: 公共基础代码
  - 日志系统 (`Logging/`)
  - 加密 (`Cryptography/`)
  - 线程管理 (`Threading/`)
  - 配置 (`Configuration/`)
  - 工具类 (`Utilities/`)

- `src/server/game/`: 游戏核心逻辑
  - `Entities/`: 游戏实体 (Player, Creature, GameObject, Item 等)
  - `AI/`: AI 系统 (CombatAI, PetAI, GuardAI 等)
  - `Spells/`: 法术和光环系统
  - `Handlers/`: 数据包处理
  - `Maps/`: 地图和实例管理
  - `Battlegrounds/`: 战场系统
  - `Combat/`: 战斗系统
  - `Loot/`: 掉落系统

- `src/server/scripts/`: 游戏脚本
  - 按地区组织: `EasternKingdoms/`, `Kalimdor/`, `Northrend/`, `Outland/`
  - 按功能组织: `Spells/`, `Commands/`, `Pet/`, `World/`
  - `Custom/`: 自定义脚本目录
  - 法术脚本: `spell_mage.cpp`, `spell_warrior.cpp` 等

- `src/server/database/`: 数据库抽象层
  - `Database/`: 数据库连接和查询
  - `Updater/`: 数据库更新系统

- `src/server/worldserver/`: 世界服务器主程序
- `src/server/authserver/`: 认证服务器主程序

- `src/tools/`: 数据提取工具
  - `map_extractor/`: 提取地图数据
  - `vmap4_extractor/`, `vmap4_assembler/`: 可视地图工具
  - `mmaps_generator/`: 移动地图生成器

- `dep/`: 第三方依赖库 (自动构建)
- `sql/`: 数据库 SQL 文件
  - `base/`: 基础数据库结构
  - `updates/`: 数据库更新脚本
  - `create/`: 数据库创建脚本

### 关键系统说明

#### 实体系统 (Entities)
所有游戏对象继承自 `Object` 基类:
- `Unit` -> `Player`, `Creature`, `Pet`
- `GameObject`: 游戏对象(箱子、门等)
- `Item`: 物品
- `Corpse`: 尸体

#### AI 系统
位于 `src/server/game/AI/`:
- `CoreAI/`: 核心 AI (战斗、被动、守卫、宠物等)
- `ScriptedAI/`: 脚本化 AI 基类
- 自定义 AI 通常在脚本中实现

#### 法术系统
- 核心逻辑: `src/server/game/Spells/`
- 法术脚本: `src/server/scripts/Spells/`
- 法术脚本按职业组织 (如 `spell_mage.cpp`, `spell_warrior.cpp`)

#### 脚本系统
脚本使用 C++ 编写,通过脚本加载器注册:
1. 创建脚本文件到 `src/server/scripts/Custom/`
2. 在 `ScriptLoader.cpp` 中添加 `AddSC` 函数调用
3. 将文件添加到 `src/server/scripts/CMakeLists.txt`

## 配置文件

服务器配置文件:
- `worldserver.conf.dist`: 世界服务器配置
- `authserver.conf.dist`: 认证服务器配置

配置文件使用 INI 格式,包含详细注释说明各选项用途。

## 数据库更新

SQL 更新文件命名格式: `YYYY_MM_DD_i_database.sql`
- `YYYY_MM_DD`: 更新日期
- `i`: 当天的第几个更新
- `database`: 目标数据库 (`world`, `characters`, `auth`)

对于 PR,使用未来日期(如 `2015_13_32_00_world.sql`)避免合并冲突。

## 测试

单元测试位于 `tests/` 目录,使用 Catch2 框架:
- `tests/common/`: 公共代码测试
- `tests/game/`: 游戏逻辑测试

## 开发工作流

### 创建新脚本

参考 `doc/HowToScript.txt` 和 `src/server/scripts/` 中的示例脚本。

### 调试技巧

- 使用 Debug 构建获取更详细的崩溃信息
- 查看日志文件 (位置在配置文件中指定)
- 使用 `--version` 参数检查服务器版本: `./worldserver --version`

### 性能分析

项目支持 jemalloc 内存分配器 (默认启用),可通过 `-DNOJEM=1` 禁用。

## 相关链接

- Wiki: https://www.trinitycore.info
- 论坛: https://talk.trinitycore.org/
- Issue 追踪: https://github.com/TrinityCore/TrinityCore/issues

---

## 全局行为约束

### 语言规则

- 所有文档、注释与回答必须使用中文。
- 禁止使用英文作为主要说明语言(专业术语除外)。
- 输出内容必须清晰、结构化,避免模糊表达。

---

### Git 操作限制

- 禁止执行 `git push`。
- 所有可能改写历史的操作必须提前说明影响,包括但不限于:
  - `git rebase`
  - `git reset --hard`
  - `git commit --amend`
  - `git filter-branch`
  - 强制合并或覆盖分支历史的操作
- 在执行可能丢失工作区内容的操作前,必须明确提示风险。
- 未经明确指示,不得擅自创建或删除分支。

---

### 文件系统安全规则

- 删除当前项目目录之外的文件前必须请求授权。
- 删除重要文件前必须提示确认。
- 批量删除操作必须提前说明影响范围。
- 未经确认,不得修改系统级文件或用户主目录文件。
- 不得假设文件存在,执行操作前必须说明依据。

---

## Claude 会话断点续接机制

### 目录结构

- `docs/claude/current.md` - 当前会话状态
- `docs/claude/history/` - 历史任务归档

### 启动检查流程

1. 每次启动必须读取 `docs/claude/current.md`。
2. 如果存在未完成任务,向用户展示:
   - 当前目标
   - 当前进度
   - 下一步计划
3. 明确询问是否继续当前任务。
4. 根据用户选择:
   - 继续任务: 恢复流程
   - 放弃任务: 归档到 history 目录,清空 current.md
   - 新任务: 确认是否覆盖 current.md

### current.md 结构

```markdown
# 当前会话状态

## 当前目标
(正在解决的核心问题)

## 当前进度
- 已完成:
- 进行中:
- 下一步:

## 关键上下文
(约束/设计决策/关键参数)

## 未解决问题
(悬而未决的点)

---
开始时间: YYYY-MM-DD HH:MM
最后更新时间: YYYY-MM-DD HH:MM
任务状态: 进行中/已中止/已完成
```

### 任务进行中规则

- 重要阶段完成后更新 current.md。
- "下一步"字段保持可执行。
- 仅记录状态与关键决策,不记录冗余对话。

### 归档规则

任务完成或中止后:
1. 复制到 `docs/claude/history/YYYY-MM-DD_HHMM_任务简述_状态.md`
2. 清空 current.md,仅保留模板结构
3. 重置任务状态为"空闲"

---

## 操作原则

- 不做隐式假设。
- 不自动执行具有不可逆影响的操作。
- 所有高风险行为必须提前说明后再执行。
- 优先保证可恢复性与可追溯性。
- 任何任务都必须可以安全中断。
- 启动时确认状态,结束时明确状态。
