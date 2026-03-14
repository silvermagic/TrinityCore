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
 * @file AllPackets.h
 * @brief 网络数据包统一包含文件
 *
 * @details 本文件作为数据包系统的统一入口点,包含所有游戏网络通信所需的数据包定义。
 *          采用伞形头文件设计模式,简化其他模块对数据包类型的引用。
 *
 * 模块职责:
 * - 集中管理所有游戏数据包的头文件包含
 * - 提供统一的数据包类型访问接口
 * - 降低模块间依赖复杂度
 *
 * 数据包系统架构:
 * - 按游戏功能域划分数据包模块(银行、角色、聊天、战斗等)
 * - 每个模块包含客户端到服务器(CMSG)和服务器到客户端(SMSG)的数据包定义
 * - 数据包使用流式读写方式处理网络数据序列化
 *
 * 性能说明:
 * - 编译时间:由于包含大量头文件,编译时间较长,建议使用预编译头(PCH)优化
 * - 使用建议:如只需特定功能的数据包,可直接包含对应的专门头文件而非本文件
 *
 * @see Packet.h 数据包基类定义
 * @see WorldPacket 世界数据包实现
 */

#ifndef AllPackets_h__
#define AllPackets_h__

// ============================================
// 游戏系统数据包包含区域
// ============================================

/**
 * @brief 银行系统数据包
 * @details 处理玩家银行操作相关的网络通信,包括:
 *          - 银行存取物品
 *          - 银行槽位购买
 *          - 银行界面交互
 */
#include "BankPackets.h"

/**
 * @brief 日历系统数据包
 * @details 处理游戏内日历功能的网络通信,包括:
 *          - 日历事件创建、修改、删除
 *          - 事件邀请和响应
 *          - 副本锁定信息同步
 */
#include "CalendarPackets.h"

/**
 * @brief 角色系统数据包
 * @details 处理角色创建、登录、更新等核心功能,包括:
 *          - 角色创建和删除
 *          - 角色列表查询
 *          - 角色登录流程
 *          - 角色数据同步(等级、经验、属性等)
 *          - 外观和模型更新
 * @note 这是游戏最基础的数据包模块之一
 */
#include "CharacterPackets.h"

/**
 * @brief 聊天系统数据包
 * @details 处理游戏内所有聊天通信功能,包括:
 *          - 各频道消息(综合、交易、本地防御等)
 *          - 私聊和密语
 *          - 公会聊天
 *          - 团队和队伍聊天
 *          - 表情和系统消息
 * @note 聊天数据包频率较高,已做优化处理
 */
#include "ChatPackets.h"

/**
 * @brief 战斗日志数据包
 * @details 处理战斗事件的详细日志记录和传输,包括:
 *          - 伤害和治疗效果
 *          - 法术施放事件
 *          - 增益和减益效果应用
 *          - 攻击命中/未命中/躲闪/招架等事件
 *          - 战斗日志过滤器设置
 * @note 数据量较大,客户端用于显示战斗信息和插件统计
 */
#include "CombatLogPackets.h"

/**
 * @brief 战斗系统数据包
 * @details 处理战斗核心机制的网络通信,包括:
 *          - 攻击开始/停止
 *          - 自动攻击状态
 *          - 武器切换
 *          - 战斗状态变更
 *          - 攻击范围和朝向更新
 * @see CombatLogPackets.h 战斗日志相关数据包
 */
#include "CombatPackets.h"

/**
 * @brief 公会系统数据包
 * @details 处理公会相关的所有网络通信,包括:
 *          - 公会创建和解散
 *          - 成员管理(邀请、踢出、晋升)
 *          - 公会银行操作
 *          - 公会等级和权限
 *          - 公会事件和新闻
 *          - 公会成就和声望
 */
#include "GuildPackets.h"

/**
 * @brief 地下城查找器(LFG)系统数据包
 * @details 处理随机副本匹配系统通信,包括:
 *          - 副本队列加入和离开
 *          - 角色选择(坦克/治疗/输出)
 *          - 随机副本奖励
 *          - 副本提议和投票
 *          - 副本完成统计
 * @note 3.3.5版本引入的重要功能
 */
#include "LFGPackets.h"

/**
 * @brief 邮件系统数据包
 * @details 处理游戏内邮件系统的网络通信,包括:
 *          - 邮件发送和接收
 *          - 附件物品和金币
 *          - 邮件列表查询
 *          - 邮件删除和标记已读
 *          - 拍卖行邮件通知
 */
#include "MailPackets.h"

/**
 * @brief 杂项数据包
 * @details 处理不属于其他专门分类的各种游戏功能,包括:
 *          - 客户端设置同步
 *          - 游戏时间查询
 *          - 特效和音效触发
 *          - 天气和光照变化
 *          - 世界 announcements
 *          - 各种辅助功能数据包
 */
#include "MiscPackets.h"

/**
 * @brief NPC交互数据包
 * @details 处理玩家与NPC交互的网络通信,包括:
 *          - NPC对话
 *          - 商店交易
 *          - 训练师服务
 *          - 银行、邮箱等服务访问
 *          - 任务NPC交互
 *          - 幻化和重铸服务
 */
#include "NPCPackets.h"

/**
 * @brief 宠物系统数据包
 * @details 处理宠物相关功能的网络通信,包括:
 *          - 宠物捕获和召唤
 *          - 宠物技能学习
 *          - 宠物状态和属性
 *          - 宠物改名和放弃
 *          - 猎人宠物喂养和快乐度
 *          - 术士和死亡骑士宠物
 * @see TotemPackets.h 图腾相关数据包
 */
#include "PetPackets.h"

/**
 * @brief 查询系统数据包
 * @details 处理客户端向服务器查询游戏对象信息的通信,包括:
 *          - 玩家名称查询
 *          - 生物模板查询
 *          - 游戏对象模板查询
 *          - 物品模板查询
 *          - 名字和提示文本查询
 * @note 用于减少初始加载数据量,按需查询优化内存使用
 */
#include "QueryPackets.h"

/**
 * @brief 任务系统数据包
 * @details 处理任务相关功能的网络通信,包括:
 *          - 任务接受和放弃
 *          - 任务完成和奖励选择
 *          - 任务进度更新
 *          - 任务日志同步
 *          - 日常任务重置
 *          - 任务目标和计数器更新
 */
#include "QuestPackets.h"

/**
 * @brief 法术系统数据包
 * @details 处理法术和技能系统的核心通信,包括:
 *          - 法术施放和打断
 *          - 法术冷却和全局冷却
 *          - 光环应用和移除
 *          - 法术学习、遗忘和升级
 *          - 法术失败和错误信息
 *          - 法术目标和效果
 * @note 法术数据包非常频繁,核心游戏机制之一
 */
#include "SpellPackets.h"

/**
 * @brief 系统数据包
 * @details 处理底层系统级网络通信,包括:
 *          - 连接建立和验证
 *          - 版本检查
 *          - 认证和会话管理
 *          - 心跳包和时间同步
 *          - 服务器状态通知
 *          - 添加 onset 和压缩设置
 * @note 这些数据包优先级最高,用于维持连接稳定性
 */
#include "SystemPackets.h"

/**
 * @brief 天赋系统数据包
 * @details 处理天赋和专精相关功能,包括:
 *          - 天赋点分配和重置
 *          - 天赋树数据同步
 *          - 双天赋系统
 *          - 天赋预览
 * @note 3.3.5版本支持双天赋系统
 */
#include "TalentPackets.h"

/**
 * @brief 图腾系统数据包
 * @details 处理萨满图腾相关功能,包括:
 *          - 图腾召唤和消失
 *          - 图腾属性和效果
 *          - 图腾位置和生命值
 *          - 图腾法术施放
 * @see PetPackets.h 其他宠物相关数据包
 */
#include "TotemPackets.h"

/**
 * @brief 世界状态数据包
 * @details 处理动态世界状态同步,包括:
 *          - PvP区域状态(Wintergrasp等)
 *          - 世界事件进度
 *          - 动态区域状态标志
 *          - 战场和竞技场状态
 *          - 世界BOSS状态
 * @note 用于客户端UI显示和世界事件触发
 */
#include "WorldStatePackets.h"

#endif // AllPackets_h__
