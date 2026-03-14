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
 * @file GameObjectData.h
 * @brief 游戏对象数据定义模块
 *
 * 本文件定义了游戏对象（GameObject）相关的核心数据结构，包括：
 * - 游戏对象模板（GameObjectTemplate）：定义游戏对象的静态属性
 * - 游戏对象数据（GameObjectData）：定义游戏对象实例的运行时数据
 * - 游戏对象附加数据（GameObjectAddon、GameObjectTemplateAddon）
 * - 四元数数据（QuaternionData）：用于游戏对象的旋转计算
 * - 游戏对象动作枚举（GameObjectActions）：定义可执行的游戏对象动作
 *
 * 这些数据结构是游戏对象系统的基础，支持多种游戏对象类型，
 * 如门、按钮、箱子、陷阱、椅子、传送门等。
 */

#ifndef GameObjectData_h__
#define GameObjectData_h__

#include "Common.h"
#include "SharedDefines.h"
#include "SpawnData.h"
#include "WorldPacket.h"

#include <array>
#include <string>
#include <vector>

/**
 * @brief 游戏对象任务物品槽位最大数量
 *
 * 定义游戏对象可关联的任务物品数量上限，用于任务相关的游戏对象交互。
 */
#define MAX_GAMEOBJECT_QUEST_ITEMS 6

/**
 * @struct GameObjectTemplate
 * @brief 游戏对象模板数据结构
 *
 * 该结构体对应数据库 `gameobject_template` 表，定义游戏对象的静态模板属性。
 * 包含游戏对象的基本信息（ID、类型、显示ID、名称等）和类型特定的数据字段。
 *
 * 使用联合体（union）存储不同类型游戏对象的特定数据，以节省内存空间。
 * 不同类型的游戏对象（门、按钮、箱子、陷阱等）有不同的数据字段布局。
 *
 * @note 该结构体在服务器启动时从数据库加载，运行期间保持只读状态。
 */
struct GameObjectTemplate
{
    uint32  entry;              ///< 游戏对象模板ID，唯一标识符
    uint32  type;               ///< 游戏对象类型（参见 GameobjectTypes 枚举）
    uint32  displayId;          ///< 显示模型ID，关联 GameObjectDisplayInfo.dbc
    std::string name;           ///< 游戏对象名称
    std::string IconName;       ///< 图标名称，用于客户端显示
    std::string castBarCaption; ///< 施法条标题文本，交互时显示
    std::string unk1;           ///< 未知字段1，保留字段
    float   size;               ///< 游戏对象缩放大小
    union                                                   ///< 不同游戏对象类型有不同的数据字段布局
    {
        /**
         * @brief 门类型游戏对象数据（GAMEOBJECT_TYPE_DOOR = 0）
         *
         * 用于实现可开关的门，支持自动关闭、锁定等功能。
         */
        struct
        {
            uint32 startOpen;                               ///< [0] 初始状态是否打开（客户端用于判断 GO_ACTIVATED 表示打开还是关闭）
            uint32 lockId;                                  ///< [1] 锁ID，关联 Lock.dbc
            uint32 autoCloseTime;                           ///< [2] 自动关闭时间（秒），实际时间 = autoCloseTime / 0x10000
            uint32 noDamageImmune;                          ///< [3] 受到伤害时是否中断打开动作
            uint32 openTextID;                              ///< [4] 打开时的文本ID（可替代 castBarCaption）
            uint32 closeTextID;                             ///< [5] 关闭时的文本ID
            uint32 ignoredByPathing;                        ///< [6] 是否被寻路系统忽略
            uint32 conditionID1;                            ///< [7] 条件ID1，用于条件判断
        } door;
        /**
         * @brief 按钮类型游戏对象数据（GAMEOBJECT_TYPE_BUTTON = 1）
         *
         * 用于实现可交互的按钮，常用于触发机关、开启通道等。
         */
        struct
        {
            uint32 startOpen;                               ///< [0] 初始状态是否按下
            uint32 lockId;                                  ///< [1] 锁ID，关联 Lock.dbc
            uint32 autoCloseTime;                           ///< [2] 自动关闭时间（秒），实际时间 = autoCloseTime / 0x10000
            uint32 linkedTrap;                              ///< [3] 关联的陷阱游戏对象ID
            uint32 noDamageImmune;                          ///< [4] 是否为战场对象（isBattlegroundObject）
            uint32 large;                                   ///< [5] 是否为大型游戏对象
            uint32 openTextID;                              ///< [6] 打开时的文本ID（可替代 castBarCaption）
            uint32 closeTextID;                             ///< [7] 关闭时的文本ID
            uint32 losOK;                                   ///< [8] 是否需要视线检查
            uint32 conditionID1;                            ///< [9] 条件ID1
        } button;
        /**
         * @brief 任务给予者类型游戏对象数据（GAMEOBJECT_TYPE_QUESTGIVER = 2）
         *
         * 用于提供任务的交互对象，如任务公告板、任务物品等。
         */
        struct
        {
            uint32 lockId;                                  ///< [0] 锁ID，关联 Lock.dbc
            uint32 questList;                               ///< [1] 任务列表ID
            uint32 pageMaterial;                            ///< [2] 页面材质ID
            uint32 gossipID;                                ///< [3] 闲聊菜单ID
            uint32 customAnim;                              ///< [4] 自定义动画ID
            uint32 noDamageImmune;                          ///< [5] 受到伤害时是否中断交互
            uint32 openTextID;                              ///< [6] 打开时的文本ID（可替代 castBarCaption）
            uint32 losOK;                                   ///< [7] 是否需要视线检查
            uint32 allowMounted;                            ///< [8] 是否允许骑乘状态下交互（0/1）
            uint32 large;                                   ///< [9] 是否为大型游戏对象
            uint32 conditionID1;                            ///< [10] 条件ID1
        } questgiver;
        /**
         * @brief 箱子类型游戏对象数据（GAMEOBJECT_TYPE_CHEST = 3）
         *
         * 用于存储可被玩家打开并获取战利品的箱子、矿脉、草药等。
         */
        struct
        {
            uint32 lockId;                                  ///< [0] 锁ID，关联 Lock.dbc
            uint32 lootId;                                  ///< [1] 战利品模板ID
            uint32 chestRestockTime;                        ///< [2] 补货时间（秒）
            uint32 consumable;                              ///< [3] 是否为消耗品（打开后消失）
            uint32 minSuccessOpens;                         ///< [4] 最小成功打开次数（已弃用，3.0前用于矿脉）
            uint32 maxSuccessOpens;                         ///< [5] 最大成功打开次数（已弃用，3.0前用于矿脉）
            uint32 eventId;                                 ///< [6] 被掠夺时触发的事件ID（lootedEvent）
            uint32 linkedTrapId;                            ///< [7] 关联的陷阱游戏对象ID
            uint32 questId;                                 ///< [8] 关联任务ID（当前未使用，存储激活所需任务）
            uint32 level;                                   ///< [9] 箱子等级
            uint32 losOK;                                   ///< [10] 是否需要视线检查
            uint32 leaveLoot;                               ///< [11] 是否保留战利品
            uint32 notInCombat;                             ///< [12] 战斗中是否无法打开
            uint32 logLoot;                                 ///< [13] 是否记录战利品日志
            uint32 openTextID;                              ///< [14] 打开时的文本ID（可替代 castBarCaption）
            uint32 groupLootRules;                          ///< [15] 是否使用团队分配规则
            uint32 floatingTooltip;                         ///< [16] 是否显示浮动提示
            uint32 conditionID1;                            ///< [17] 条件ID1
        } chest;
        //4 GAMEOBJECT_TYPE_BINDER - 空结构（绑定器类型，未使用）

        /**
         * @brief 通用类型游戏对象数据（GAMEOBJECT_TYPE_GENERIC = 5）
         *
         * 用于一般性的装饰或功能性游戏对象，无特殊行为。
         */
        struct
        {
            uint32 floatingTooltip;                         ///< [0] 是否显示浮动提示
            uint32 highlight;                               ///< [1] 是否高亮显示
            uint32 serverOnly;                              ///< [2] 是否仅服务器端使用
            uint32 large;                                   ///< [3] 是否为大型游戏对象
            uint32 floatOnWater;                            ///< [4] 是否漂浮在水面上
            int32 questID;                                  ///< [5] 关联任务ID
            uint32 conditionID1;                            ///< [6] 条件ID1
        } _generic;
        /**
         * @brief 陷阱类型游戏对象数据（GAMEOBJECT_TYPE_TRAP = 6）
         *
         * 用于实现可触发陷阱，可对附近单位施放法术。
         */
        struct
        {
            uint32 lockId;                                  ///< [0] 锁ID，关联 Lock.dbc
            uint32 level;                                   ///< [1] 陷阱等级
            uint32 diameter;                                ///< [2] 触发直径（陷阱激活范围）
            uint32 spellId;                                 ///< [3] 触发时施放的法术ID
            uint32 type;                                    ///< [4] 陷阱类型：0=施法后不消失，1=施法后消失，2=生成时施法（炸弹）
            uint32 cooldown;                                ///< [5] 冷却时间（秒）
            int32 autoCloseTime;                            ///< [6] 自动关闭时间
            uint32 startDelay;                              ///< [7] 启动延迟（秒）
            uint32 serverOnly;                              ///< [8] 是否仅服务器端使用
            uint32 stealthed;                               ///< [9] 是否潜行状态
            uint32 large;                                   ///< [10] 是否为大型游戏对象
            uint32 invisible;                               ///< [11] 是否隐形状态
            uint32 openTextID;                              ///< [12] 打开时的文本ID（可替代 castBarCaption）
            uint32 closeTextID;                             ///< [13] 关闭时的文本ID
            uint32 ignoreTotems;                            ///< [14] 是否忽略图腾
            uint32 conditionID1;                            ///< [15] 条件ID1
        } trap;
        /**
         * @brief 椅子类型游戏对象数据（GAMEOBJECT_TYPE_CHAIR = 7）
         *
         * 用于实现玩家可以坐下的椅子，支持多座位。
         */
        struct
        {
            uint32 slots;                                   ///< [0] 座位数量
            uint32 height;                                  ///< [1] 椅子高度
            uint32 onlyCreatorUse;                          ///< [2] 是否仅创建者可用
            uint32 triggeredEvent;                          ///< [3] 触发的事件ID
            uint32 conditionID1;                            ///< [4] 条件ID1
        } chair;
        /**
         * @brief 法术焦点类型游戏对象数据（GAMEOBJECT_TYPE_SPELL_FOCUS = 8）
         *
         * 用于定义法术施放所需的焦点对象，某些法术需要在特定位置施放。
         */
        struct
        {
            uint32 focusId;                                 ///< [0] 法术焦点ID
            uint32 dist;                                    ///< [1] 生效距离
            uint32 linkedTrapId;                            ///< [2] 关联的陷阱游戏对象ID
            uint32 serverOnly;                              ///< [3] 是否仅服务器端使用
            uint32 questID;                                 ///< [4] 关联任务ID
            uint32 large;                                   ///< [5] 是否为大型游戏对象
            uint32 floatingTooltip;                         ///< [6] 是否显示浮动提示
            uint32 floatOnWater;                            ///< [7] 是否漂浮在水面上
            uint32 conditionID1;                            ///< [8] 条件ID1
        } spellFocus;
        /**
         * @brief 文本类型游戏对象数据（GAMEOBJECT_TYPE_TEXT = 9）
         *
         * 用于显示可阅读的文本内容，如书籍、告示牌等。
         */
        struct
        {
            uint32 pageID;                                  ///< [0] 页面ID
            uint32 language;                                ///< [1] 语言类型
            uint32 pageMaterial;                            ///< [2] 页面材质ID
            uint32 allowMounted;                            ///< [3] 是否允许骑乘状态下阅读（0/1）
            uint32 conditionID1;                            ///< [4] 条件ID1
        } text;
        /**
         * @brief Goober类型游戏对象数据（GAMEOBJECT_TYPE_GOOBER = 10）
         *
         * 用于通用的交互对象，如任务物品、机关、开关等。
         * 这是最灵活的游戏对象类型之一，支持多种交互行为。
         */
        struct
        {
            uint32 lockId;                                  ///< [0] 锁ID，关联 Lock.dbc
            int32 questId;                                  ///< [1] 关联任务ID
            uint32 eventId;                                 ///< [2] 触发的事件ID
            uint32 autoCloseTime;                           ///< [3] 自动关闭时间（秒）
            uint32 customAnim;                              ///< [4] 自定义动画ID
            uint32 consumable;                              ///< [5] 是否为消耗品（使用后消失）
            uint32 cooldown;                                ///< [6] 冷却时间（秒）
            uint32 pageId;                                  ///< [7] 页面ID
            uint32 language;                                ///< [8] 语言类型
            uint32 pageMaterial;                            ///< [9] 页面材质ID
            uint32 spellId;                                 ///< [10] 施放的法术ID
            uint32 noDamageImmune;                          ///< [11] 受到伤害时是否中断交互
            uint32 linkedTrapId;                            ///< [12] 关联的陷阱游戏对象ID
            uint32 large;                                   ///< [13] 是否为大型游戏对象
            uint32 openTextID;                              ///< [14] 打开时的文本ID（可替代 castBarCaption）
            uint32 closeTextID;                             ///< [15] 关闭时的文本ID
            uint32 losOK;                                   ///< [16] 是否为战场对象（isBattlegroundObject）
            uint32 allowMounted;                            ///< [17] 是否允许骑乘状态下交互（0/1）
            uint32 floatingTooltip;                         ///< [18] 是否显示浮动提示
            uint32 gossipID;                                ///< [19] 闲聊菜单ID
            uint32 WorldStateSetsState;                     ///< [20] 世界状态设置状态
            uint32 floatOnWater;                            ///< [21] 是否漂浮在水面上
            uint32 conditionID1;                            ///< [22] 条件ID1
        } goober;
        /**
         * @brief 运输工具类型游戏对象数据（GAMEOBJECT_TYPE_TRANSPORT = 11）
         *
         * 用于实现可移动的运输工具，如电梯、船只、飞艇等。
         */
        struct
        {
            uint32 pause;                                   ///< [0] 暂停时间
            uint32 startOpen;                               ///< [1] 初始状态是否打开
            uint32 autoCloseTime;                           ///< [2] 自动关闭时间（秒），实际时间 = autoCloseTime / 0x10000
            uint32 pause1EventID;                           ///< [3] 暂停点1事件ID
            uint32 pause2EventID;                           ///< [4] 暂停点2事件ID
            uint32 mapID;                                   ///< [5] 地图ID
        } transport;
        /**
         * @brief 区域伤害类型游戏对象数据（GAMEOBJECT_TYPE_AREADAMAGE = 12）
         *
         * 用于对区域内单位造成持续伤害的游戏对象。
         */
        struct
        {
            uint32 lockId;                                  ///< [0] 锁ID
            uint32 radius;                                  ///< [1] 伤害半径
            uint32 damageMin;                               ///< [2] 最小伤害值
            uint32 damageMax;                               ///< [3] 最大伤害值
            uint32 damageSchool;                            ///< [4] 伤害类型（神圣、火焰、冰霜等）
            uint32 autoCloseTime;                           ///< [5] 自动关闭时间（秒），实际时间 = autoCloseTime / 0x10000
            uint32 openTextID;                              ///< [6] 打开时的文本ID
            uint32 closeTextID;                             ///< [7] 关闭时的文本ID
        } areadamage;
        /**
         * @brief 摄像机类型游戏对象数据（GAMEOBJECT_TYPE_CAMERA = 13）
         *
         * 用于触发过场动画或特殊摄像机视角的游戏对象。
         */
        struct
        {
            uint32 lockId;                                  ///< [0] 锁ID，关联 Lock.dbc
            uint32 cinematicId;                             ///< [1] 过场动画ID
            uint32 eventID;                                 ///< [2] 触发的事件ID
            uint32 openTextID;                              ///< [3] 打开时的文本ID（可替代 castBarCaption）
            uint32 conditionID1;                            ///< [4] 条件ID1
        } camera;
        //14 GAMEOBJECT_TYPE_MAPOBJECT - 空结构（地图对象类型，未使用）
        /**
         * @brief 移动运输工具类型游戏对象数据（GAMEOBJECT_TYPE_MO_TRANSPORT = 15）
         *
         * 用于实现沿固定路径移动的运输工具，如船只、飞艇、矿道地铁等。
         */
        struct
        {
            uint32 taxiPathId;                              ///< [0] 移动路径ID（出租车路径）
            uint32 moveSpeed;                               ///< [1] 移动速度
            uint32 accelRate;                               ///< [2] 加速度
            uint32 startEventID;                            ///< [3] 启动事件ID
            uint32 stopEventID;                             ///< [4] 停止事件ID
            uint32 transportPhysics;                        ///< [5] 运输工具物理属性
            uint32 mapID;                                   ///< [6] 地图ID
            uint32 worldState1;                             ///< [7] 世界状态1
            uint32 canBeStopped;                            ///< [8] 是否可以被停止
        } moTransport;
        //16 GAMEOBJECT_TYPE_DUELFLAG - 空结构（决斗旗帜类型）
        //17 GAMEOBJECT_TYPE_FISHINGNODE - 空结构（钓鱼节点类型）
        /**
         * @brief 召唤仪式类型游戏对象数据（GAMEOBJECT_TYPE_SUMMONING_RITUAL = 18）
         *
         * 用于需要多名玩家参与的召唤仪式，如召唤深渊领主等。
         */
        struct
        {
            uint32 reqParticipants;                         ///< [0] 需要的参与者数量
            uint32 spellId;                                 ///< [1] 召唤法术ID
            uint32 animSpell;                               ///< [2] 动画法术ID
            uint32 ritualPersistent;                        ///< [3] 仪式是否持续存在
            uint32 casterTargetSpell;                       ///< [4] 施法者目标法术ID
            uint32 casterTargetSpellTargets;                ///< [5] 施法者目标法术目标类型
            uint32 castersGrouped;                          ///< [6] 施法者是否需要组队
            uint32 ritualNoTargetCheck;                     ///< [7] 是否不检查目标
            uint32 conditionID1;                            ///< [8] 条件ID1
        } summoningRitual;
        /**
         * @brief 邮箱类型游戏对象数据（GAMEOBJECT_TYPE_MAILBOX = 19）
         *
         * 用于玩家收发邮件的邮箱。
         */
        struct
        {
            uint32 conditionID1;                            ///< [0] 条件ID1
        } mailbox;
        //20 GAMEOBJECT_TYPE_DO_NOT_USE - 空结构（未使用的类型）
        /**
         * @brief 守卫岗哨类型游戏对象数据（GAMEOBJECT_TYPE_GUARDPOST = 21）
         *
         * 用于生成守卫NPC的岗哨点。
         */
        struct
        {
            uint32 creatureID;                              ///< [0] 生成的生物ID
            uint32 charges;                                 ///< [1] 充能次数（可使用次数）
        } guardpost;
        /**
         * @brief 法术施放者类型游戏对象数据（GAMEOBJECT_TYPE_SPELLCASTER = 22）
         *
         * 用于对使用者施放法术的游戏对象，如治疗之泉、法力之泉图腾等。
         */
        struct
        {
            uint32 spellId;                                 ///< [0] 施放的法术ID
            uint32 charges;                                 ///< [1] 充能次数（0表示无限）
            uint32 partyOnly;                               ///< [2] 是否仅对小队成员有效
            uint32 allowMounted;                            ///< [3] 是否允许骑乘状态下交互（0/1）
            uint32 large;                                   ///< [4] 是否为大型游戏对象
            uint32 conditionID1;                            ///< [5] 条件ID1
        } spellcaster;
        /**
         * @brief 集合石类型游戏对象数据（GAMEOBJECT_TYPE_MEETINGSTONE = 23）
         *
         * 用于组队匹配的集合石，允许玩家寻找副本队伍。
         */
        struct
        {
            uint32 minLevel;                                ///< [0] 最小等级要求
            uint32 maxLevel;                                ///< [1] 最大等级要求
            uint32 areaID;                                  ///< [2] 区域ID
        } meetingstone;
        /**
         * @brief 旗座类型游戏对象数据（GAMEOBJECT_TYPE_FLAGSTAND = 24）
         *
         * 用于战场中的旗帜基座，玩家可从中拾取旗帜。
         */
        struct
        {
            uint32 lockId;                                  ///< [0] 锁ID
            uint32 pickupSpell;                             ///< [1] 拾取时施放的法术ID
            uint32 radius;                                  ///< [2] 拾取半径
            uint32 returnAura;                              ///< [3] 返回时光环ID
            uint32 returnSpell;                             ///< [4] 返回时法术ID
            uint32 noDamageImmune;                          ///< [5] 免疫状态下是否无法拾取
            uint32 openTextID;                              ///< [6] 打开时的文本ID
            uint32 losOK;                                   ///< [7] 是否需要视线检查
            uint32 conditionID1;                            ///< [8] 条件ID1
        } flagstand;
        /**
         * @brief 钓鱼洞类型游戏对象数据（GAMEOBJECT_TYPE_FISHINGHOLE = 25）
         *
         * 用于定义钓鱼区域的鱼群，玩家可在其中钓鱼获取特定战利品。
         */
        struct
        {
            uint32 radius;                                  ///< [0] 钓鱼有效半径（浮标需落在此范围内）
            uint32 lootId;                                  ///< [1] 战利品模板ID
            uint32 minSuccessOpens;                         ///< [2] 最小成功开启次数
            uint32 maxSuccessOpens;                         ///< [3] 最大成功开启次数
            uint32 lockId;                                  ///< [4] 锁ID，关联 Lock.dbc（通常为1628）
        } fishinghole;
        /**
         * @brief 掉落旗帜类型游戏对象数据（GAMEOBJECT_TYPE_FLAGDROP = 26）
         *
         * 用于战场上掉落的旗帜，玩家可拾取并返回。
         */
        struct
        {
            uint32 lockId;                                  ///< [0] 锁ID
            uint32 eventID;                                 ///< [1] 触发的事件ID
            uint32 pickupSpell;                             ///< [2] 拾取时施放的法术ID
            uint32 noDamageImmune;                          ///< [3] 免疫状态下是否无法拾取
            uint32 openTextID;                              ///< [4] 打开时的文本ID
        } flagdrop;
        /**
         * @brief 小游戏类型游戏对象数据（GAMEOBJECT_TYPE_MINI_GAME = 27）
         *
         * 用于实现小游戏功能。
         */
        struct
        {
            uint32 gameType;                                ///< [0] 游戏类型
        } miniGame;
        //28 GAMEOBJECT_TYPE_DO_NOT_USE_2 - 空结构（未使用的类型）
        /**
         * @brief 占领点类型游戏对象数据（GAMEOBJECT_TYPE_CAPTURE_POINT = 29）
         *
         * 用于战场和世界PVP中的可占领据点，支持阵营争夺机制。
         */
        struct
        {
            uint32 radius;                                  ///< [0] 占领半径
            uint32 spell;                                   ///< [1] 占领时施放的法术ID
            uint32 worldState1;                             ///< [2] 世界状态1
            uint32 worldstate2;                             ///< [3] 世界状态2
            uint32 winEventID1;                             ///< [4] 获胜事件ID1
            uint32 winEventID2;                             ///< [5] 获胜事件ID2
            uint32 contestedEventID1;                       ///< [6] 争夺事件ID1
            uint32 contestedEventID2;                       ///< [7] 争夺事件ID2
            uint32 progressEventID1;                        ///< [8] 进度事件ID1
            uint32 progressEventID2;                        ///< [9] 进度事件ID2
            uint32 neutralEventID1;                         ///< [10] 中立事件ID1
            uint32 neutralEventID2;                         ///< [11] 中立事件ID2
            uint32 neutralPercent;                          ///< [12] 中立百分比
            uint32 worldstate3;                             ///< [13] 世界状态3
            uint32 minSuperiority;                          ///< [14] 最小优势值
            uint32 maxSuperiority;                          ///< [15] 最大优势值
            uint32 minTime;                                 ///< [16] 最小占领时间
            uint32 maxTime;                                 ///< [17] 最大占领时间
            uint32 large;                                   ///< [18] 是否为大型游戏对象
            uint32 highlight;                               ///< [19] 是否高亮显示
            uint32 startingValue;                           ///< [20] 起始值
            uint32 unidirectional;                          ///< [21] 是否单向占领
        } capturePoint;
        /**
         * @brief 光环生成器类型游戏对象数据（GAMEOBJECT_TYPE_AURA_GENERATOR = 30）
         *
         * 用于对范围内单位施加光环效果的游戏对象。
         */
        struct
        {
            uint32 startOpen;                               ///< [0] 初始状态是否激活
            uint32 radius;                                  ///< [1] 光环生效半径
            uint32 auraID1;                                 ///< [2] 光环ID1
            uint32 conditionID1;                            ///< [3] 条件ID1（光环1的触发条件）
            uint32 auraID2;                                 ///< [4] 光环ID2
            uint32 conditionID2;                            ///< [5] 条件ID2（光环2的触发条件）
            uint32 serverOnly;                              ///< [6] 是否仅服务器端使用
        } auraGenerator;
        /**
         * @brief 地下城难度类型游戏对象数据（GAMEOBJECT_TYPE_DUNGEON_DIFFICULTY = 31）
         *
         * 用于切换副本难度的游戏对象（如地下城入口处的难度选择器）。
         */
        struct
        {
            uint32 mapID;                                   ///< [0] 地图ID
            uint32 difficulty;                              ///< [1] 难度设置
        } dungeonDifficulty;
        /**
         * @brief 理发椅类型游戏对象数据（GAMEOBJECT_TYPE_BARBER_CHAIR = 32）
         *
         * 用于理发店的椅子，玩家可在此修改外观。
         */
        struct
        {
            uint32 chairheight;                             ///< [0] 椅子高度
            uint32 heightOffset;                            ///< [1] 高度偏移量
        } barberChair;
        /**
         * @brief 可破坏建筑类型游戏对象数据（GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING = 33）
         *
         * 用于战场和世界PVP中可被玩家破坏的建筑，如城墙、塔楼等。
         * 支持完整、受损、摧毁三种状态，每种状态可触发不同事件。
         */
        struct
        {
            uint32 intactNumHits;                           ///< [0] 完整状态下需要的攻击次数
            uint32 creditProxyCreature;                     ///< [1] 击杀奖励代理生物ID
            uint32 empty1;                                  ///< [2] 保留字段
            uint32 intactEvent;                             ///< [3] 完整状态下的事件ID
            uint32 empty2;                                  ///< [4] 保留字段
            uint32 damagedNumHits;                          ///< [5] 受损状态下需要的攻击次数
            uint32 empty3;                                  ///< [6] 保留字段
            uint32 empty4;                                  ///< [7] 保留字段
            uint32 empty5;                                  ///< [8] 保留字段
            uint32 damagedEvent;                            ///< [9] 受损状态下的事件ID
            uint32 empty6;                                  ///< [10] 保留字段
            uint32 empty7;                                  ///< [11] 保留字段
            uint32 empty8;                                  ///< [12] 保留字段
            uint32 empty9;                                  ///< [13] 保留字段
            uint32 destroyedEvent;                          ///< [14] 摧毁状态下的事件ID
            uint32 empty10;                                 ///< [15] 保留字段
            uint32 rebuildingTimeSecs;                      ///< [16] 重建时间（秒）
            uint32 empty11;                                 ///< [17] 保留字段
            uint32 destructibleData;                        ///< [18] 可破坏数据ID
            uint32 rebuildingEvent;                         ///< [19] 重建事件ID
            uint32 empty12;                                 ///< [20] 保留字段
            uint32 empty13;                                 ///< [21] 保留字段
            uint32 damageEvent;                             ///< [22] 受损事件ID
            uint32 empty14;                                 ///< [23] 保留字段
        } building;
        /**
         * @brief 公会银行类型游戏对象数据（GAMEOBJECT_TYPE_GUILDBANK = 34）
         *
         * 用于访问公会银行的游戏对象。
         */
        struct
        {
            uint32 conditionID1;                            ///< [0] 条件ID1
        } guildbank;
        /**
         * @brief 活板门类型游戏对象数据（GAMEOBJECT_TYPE_TRAPDOOR = 35）
         *
         * 用于可开启/关闭的活板门，常用于陷阱或隐藏通道。
         */
        struct
        {
            uint32 whenToPause;                             ///< [0] 暂停时机
            uint32 startOpen;                               ///< [1] 初始状态是否打开
            uint32 autoClose;                               ///< [2] 自动关闭时间
        } trapDoor;

        /**
         * @brief 原始数据结构
         *
         * 不用于特定字段访问，仅用于循环遍历所有字段。
         * 同时此结构确定了联合体的最大大小。
         */
        struct
        {
            uint32 data[MAX_GAMEOBJECT_DATA];               ///< 原始数据数组
        } raw;
    };

    std::string AIName;                                    ///< AI名称（标识使用的AI脚本）
    uint32 ScriptId;                                       ///< 脚本ID
    WorldPacket QueryData[TOTAL_LOCALES];                  ///< 查询数据包缓存（按语言区域索引）

    /**
     * @brief 检查游戏对象是否在动作后消失
     * @return 如果动作后消失返回 true，否则返回 false
     *
     * 某些游戏对象（如消耗性箱子、一次性机关）在使用后会消失。
     * 仅对 GAMEOBJECT_TYPE_CHEST 和 GAMEOBJECT_TYPE_GOOBER 类型有效。
     *
     * @note 性能：O(1) 时间复杂度，简单类型判断
     */
    bool IsDespawnAtAction() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_CHEST:  return chest.consumable != 0;
            case GAMEOBJECT_TYPE_GOOBER: return goober.consumable != 0;
            default: return false;
        }
    }

    /**
     * @brief 检查游戏对象是否可在骑乘状态下使用
     * @return 如果可以骑乘时使用返回 true，否则返回 false
     *
     * 某些游戏对象允许玩家在骑乘坐骑或驾驶载具时交互。
     * 不同类型的游戏对象有不同的骑乘交互规则。
     *
     * @note 邮箱默认可在骑乘状态下使用，理发椅默认不可
     */
    bool IsUsableMounted() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_MAILBOX: return true;
            case GAMEOBJECT_TYPE_BARBER_CHAIR: return false;
            case GAMEOBJECT_TYPE_QUESTGIVER: return questgiver.allowMounted != 0;
            case GAMEOBJECT_TYPE_TEXT: return text.allowMounted != 0;
            case GAMEOBJECT_TYPE_GOOBER: return goober.allowMounted != 0;
            case GAMEOBJECT_TYPE_SPELLCASTER: return spellcaster.allowMounted != 0;
            default: return false;
        }
    }

    /**
     * @brief 检查游戏对象是否忽略视线检查（LOS）
     * @return 如果忽略视线检查返回 true，否则返回 false
     *
     * 某些游戏对象可以在视线被阻挡的情况下交互（如透过墙壁）。
     * 陷阱类型游戏对象总是忽略视线检查。
     *
     * @note losOK 字段为0表示忽略视线检查，非0表示需要视线检查
     */
    bool IsIgnoringLOSChecks() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_BUTTON: return button.losOK == 0;
            case GAMEOBJECT_TYPE_QUESTGIVER: return questgiver.losOK == 0;
            case GAMEOBJECT_TYPE_CHEST: return chest.losOK == 0;
            case GAMEOBJECT_TYPE_GOOBER: return goober.losOK == 0;
            case GAMEOBJECT_TYPE_FLAGSTAND: return flagstand.losOK == 0;
            case GAMEOBJECT_TYPE_TRAP: return true;
            default: return false;
        }
    }

    /**
     * @brief 获取游戏对象的锁ID
     * @return 锁ID，如果该类型不支持锁定则返回0
     *
     * 锁ID用于关联 Lock.dbc，定义解锁所需的钥匙或技能。
     * 多种游戏对象类型支持锁定功能，如门、按钮、箱子等。
     *
     * @note 性能：O(1) 时间复杂度
     */
    uint32 GetLockId() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_DOOR:       return door.lockId;
            case GAMEOBJECT_TYPE_BUTTON:     return button.lockId;
            case GAMEOBJECT_TYPE_QUESTGIVER: return questgiver.lockId;
            case GAMEOBJECT_TYPE_CHEST:      return chest.lockId;
            case GAMEOBJECT_TYPE_TRAP:       return trap.lockId;
            case GAMEOBJECT_TYPE_GOOBER:     return goober.lockId;
            case GAMEOBJECT_TYPE_AREADAMAGE: return areadamage.lockId;
            case GAMEOBJECT_TYPE_CAMERA:     return camera.lockId;
            case GAMEOBJECT_TYPE_FLAGSTAND:  return flagstand.lockId;
            case GAMEOBJECT_TYPE_FISHINGHOLE:return fishinghole.lockId;
            case GAMEOBJECT_TYPE_FLAGDROP:   return flagdrop.lockId;
            default: return 0;
        }
    }

    /**
     * @brief 检查游戏对象是否可能在被目标锁定时消失
     * @return 如果可能消失返回 true，否则返回 false
     *
     * 此函数检查游戏对象是否在施法目标锁定时触发消失行为。
     * 主要用于免疫效果下的交互判断。
     */
    bool GetDespawnPossibility() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_DOOR:       return door.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_BUTTON:     return button.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_QUESTGIVER: return questgiver.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_GOOBER:     return goober.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_FLAGSTAND:  return flagstand.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_FLAGDROP:   return flagdrop.noDamageImmune != 0;
            default: return true;
        }
    }

    /**
     * @brief 检查免疫效果下的玩家是否无法使用该游戏对象
     * @return 如果免疫状态下无法使用返回 true，否则返回 false
     *
     * 某些游戏对象无法在免疫效果（如圣盾术）激活时交互。
     * 对于箱子类型，所有箱子在3.3.5a版本中都不允许免疫状态下打开。
     *
     * @note 时机：在玩家尝试交互时检查
     */
    bool CannotBeUsedUnderImmunity() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_DOOR:       return door.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_BUTTON:     return button.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_QUESTGIVER: return questgiver.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_CHEST:      return true;                           // All chests cannot be opened while immune on 3.3.5a
            case GAMEOBJECT_TYPE_GOOBER:     return goober.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_FLAGSTAND:  return flagstand.noDamageImmune != 0;
            case GAMEOBJECT_TYPE_FLAGDROP:   return flagdrop.noDamageImmune != 0;
            default: return false;
        }
    }

    /**
     * @brief 获取游戏对象的充能次数
     * @return 充能次数，如果不支持充能则返回0
     *
     * 某些游戏对象（如守卫岗哨、法术施放者）有使用次数限制。
     * 当充能次数耗尽时，游戏对象可能会消失。
     */
    uint32 GetCharges() const
    {
        switch (type)
        {
            //case GAMEOBJECT_TYPE_TRAP:        return trap.charges;
            case GAMEOBJECT_TYPE_GUARDPOST:   return guardpost.charges;
            case GAMEOBJECT_TYPE_SPELLCASTER: return spellcaster.charges;
            default: return 0;
        }
    }

    /**
     * @brief 获取关联的游戏对象模板ID
     * @return 关联的游戏对象ID，如果没有关联则返回0
     *
     * 某些游戏对象会触发关联的其他游戏对象，如按钮触发陷阱。
     * 用于实现连锁机关或复合交互机制。
     */
    uint32 GetLinkedGameObjectEntry() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_BUTTON:      return button.linkedTrap;
            case GAMEOBJECT_TYPE_CHEST:       return chest.linkedTrapId;
            case GAMEOBJECT_TYPE_SPELL_FOCUS: return spellFocus.linkedTrapId;
            case GAMEOBJECT_TYPE_GOOBER:      return goober.linkedTrapId;
            default: return 0;
        }
    }

    /**
     * @brief 获取游戏对象的自动关闭时间
     * @return 自动关闭时间（原始值），如果不支持自动关闭则返回0
     *
     * 某些游戏对象（如门、按钮）在打开后会自动关闭。
     * 返回值为原始数据值，实际时间需根据版本进行处理。
     * 注意：3.0.3版本之前需要除以 0x10000 进行转换。
     *
     * @note 时机：游戏对象打开后开始计时
     */
    uint32 GetAutoCloseTime() const
    {
        uint32 autoCloseTime = 0;
        switch (type)
        {
            case GAMEOBJECT_TYPE_DOOR:          autoCloseTime = door.autoCloseTime; break;
            case GAMEOBJECT_TYPE_BUTTON:        autoCloseTime = button.autoCloseTime; break;
            case GAMEOBJECT_TYPE_TRAP:          autoCloseTime = trap.autoCloseTime; break;
            case GAMEOBJECT_TYPE_GOOBER:        autoCloseTime = goober.autoCloseTime; break;
            case GAMEOBJECT_TYPE_TRANSPORT:     autoCloseTime = transport.autoCloseTime; break;
            case GAMEOBJECT_TYPE_AREADAMAGE:    autoCloseTime = areadamage.autoCloseTime; break;
            default: break;
        }
        return autoCloseTime;              // prior to 3.0.3, conversion was / 0x10000;
    }

    uint32 GetLootId() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_CHEST:       return chest.lootId;
            case GAMEOBJECT_TYPE_FISHINGHOLE: return fishinghole.lootId;
            default: return 0;
        }
    }

    uint32 GetGossipMenuId() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_QUESTGIVER:    return questgiver.gossipID;
            case GAMEOBJECT_TYPE_GOOBER:        return goober.gossipID;
            default: return 0;
        }
    }

    uint32 GetEventScriptId() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_GOOBER:        return goober.eventId;
            case GAMEOBJECT_TYPE_CHEST:         return chest.eventId;
            case GAMEOBJECT_TYPE_CAMERA:        return camera.eventID;
            default: return 0;
        }
    }

    uint32 GetCooldown() const                              // Cooldown preventing goober and traps to cast spell
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_TRAP:        return trap.cooldown;
            case GAMEOBJECT_TYPE_GOOBER:      return goober.cooldown;
            default: return 0;
        }
    }

    bool IsLargeGameObject() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_BUTTON:            return button.large != 0;
            case GAMEOBJECT_TYPE_QUESTGIVER:        return questgiver.large != 0;
            case GAMEOBJECT_TYPE_GENERIC:           return _generic.large != 0;
            case GAMEOBJECT_TYPE_TRAP:              return trap.large != 0;
            case GAMEOBJECT_TYPE_SPELL_FOCUS:       return spellFocus.large != 0;
            case GAMEOBJECT_TYPE_GOOBER:            return goober.large != 0;
            case GAMEOBJECT_TYPE_SPELLCASTER:       return spellcaster.large != 0;
            case GAMEOBJECT_TYPE_CAPTURE_POINT:     return capturePoint.large != 0;
            default: return false;
        }
    }

    bool IsInfiniteGameObject() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_DOOR:                  return true;
            case GAMEOBJECT_TYPE_FLAGSTAND:             return true;
            case GAMEOBJECT_TYPE_FLAGDROP:              return true;
            case GAMEOBJECT_TYPE_TRAPDOOR:              return true;
            default: return false;
        }
    }

    uint32 GetServerOnly() const
    {
        switch (type)
        {
            case GAMEOBJECT_TYPE_GENERIC: return _generic.serverOnly;
            case GAMEOBJECT_TYPE_TRAP: return trap.serverOnly;
            case GAMEOBJECT_TYPE_SPELL_FOCUS: return spellFocus.serverOnly;
            case GAMEOBJECT_TYPE_AURA_GENERATOR: return auraGenerator.serverOnly;
            default: return 0;
        }
    }

    void InitializeQueryData();
    WorldPacket BuildQueryData(LocaleConstant loc) const;
};

// From `gameobject_template_addon`, `gameobject_overrides`
struct GameObjectOverride
{
    uint32 Faction;
    uint32 Flags;
};

// From `gameobject_template_addon`
struct GameObjectTemplateAddon : public GameObjectOverride
{
    uint32 Mingold = 0;
    uint32 Maxgold = 0;
    std::array<uint32, 4> artKits = {};
};

struct GameObjectLocale
{
    std::vector<std::string> Name;
    std::vector<std::string> CastBarCaption;
};

struct TC_GAME_API QuaternionData
{
    float x, y, z, w;

    QuaternionData() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) { }
    QuaternionData(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) { }

    bool isUnit() const;
    void toEulerAnglesZYX(float& Z, float& Y, float& X) const;
    static QuaternionData fromEulerAnglesZYX(float Z, float Y, float X);

    friend bool operator==(QuaternionData const& left, QuaternionData const& right) = default;
};

// `gameobject_addon` table
struct GameObjectAddon
{
    QuaternionData ParentRotation;
    InvisibilityType invisibilityType;
    uint32 InvisibilityValue;
};

// `gameobject` table
struct GameObjectData : public SpawnData
{
    GameObjectData() : SpawnData(SPAWN_TYPE_GAMEOBJECT) { }
    QuaternionData rotation;
    uint32 animprogress = 0;
    GOState goState = GO_STATE_ACTIVE;
    uint8 artKit = 0;
};

enum class GameObjectActions : uint32
{
                                    // Name from client executable      // Comments
    None                     = 0,   // -NONE-
    AnimateCustom0           = 1,   // Animate Custom0
    AnimateCustom1           = 2,   // Animate Custom1
    AnimateCustom2           = 3,   // Animate Custom2
    AnimateCustom3           = 4,   // Animate Custom3
    Disturb                  = 5,   // Disturb                          // Triggers trap
    Unlock                   = 6,   // Unlock                           // Resets GO_FLAG_LOCKED
    Lock                     = 7,   // Lock                             // Sets GO_FLAG_LOCKED
    Open                     = 8,   // Open                             // Sets GO_STATE_ACTIVE
    OpenAndUnlock            = 9,   // Open + Unlock                    // Sets GO_STATE_ACTIVE and resets GO_FLAG_LOCKED
    Close                    = 10,  // Close                            // Sets GO_STATE_READY
    ToggleOpen               = 11,  // Toggle Open
    Destroy                  = 12,  // Destroy                          // Sets GO_STATE_DESTROYED
    Rebuild                  = 13,  // Rebuild                          // Resets from GO_STATE_DESTROYED
    Creation                 = 14,  // Creation
    Despawn                  = 15,  // Despawn
    MakeInert                = 16,  // Make Inert                       // Disables interactions
    MakeActive               = 17,  // Make Active                      // Enables interactions
    CloseAndLock             = 18,  // Close + Lock                     // Sets GO_STATE_READY and sets GO_FLAG_LOCKED
    UseArtKit0               = 19,  // Use ArtKit0                      // 46904: 121
    UseArtKit1               = 20,  // Use ArtKit1                      // 36639: 81, 46903: 122
    UseArtKit2               = 21,  // Use ArtKit2
    UseArtKit3               = 22,  // Use ArtKit3
    SetTapList               = 23,  // Set Tap List
    Max
};

#endif // GameObjectData_h__
