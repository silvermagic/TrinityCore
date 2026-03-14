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
 * @file SkillHandler.cpp
 * @brief 天赋和技能学习处理器模块
 *
 * @模块职责:
 *   处理玩家天赋和技能学习相关的网络消息，包括：
 *   - 学习单个天赋
 *   - 批量学习预览天赋
 *   - 重置天赋（洗点）
 *   - 遗忘技能
 *
 * @主要功能:
 *   1. 天赋系统：处理天赋点的分配和重置
 *   2. 预览天赋：支持批量学习天赋的功能
 *   3. 技能遗忘：允许玩家遗忘已学习的专业技能
 *   4. 洗点系统：通过NPC重置天赋并收取金币费用
 *
 * @性能注意事项:
 *   - 学习天赋操作会立即更新数据库
 *   - 洗点操作涉及金币交易，需要严格的验证
 *   - 批量学习天赋有最大数量限制（150个）以防止恶意数据包
 */

#include "WorldSession.h"
#include "Common.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "TalentPackets.h"
#include "WorldPacket.h"

/**
 * @brief 处理学习单个天赋操作码
 *
 * @职责:
 *   处理玩家学习单个天赋的网络请求。玩家在天赋界面点击天赋图标时发送此消息。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含：
 *              - talent_id: 天赋ID（在Talent.dbc中定义）
 *              - requested_rank: 请求的天赋等级（从0开始）
 *
 * @返回值: 无
 *
 * @调用时机:
 *   玩家在天赋界面中点击某个天赋图标时，客户端发送CMSG_LEARN_TALENT消息。
 *
 * @主要流程:
 *   1. 从数据包中读取天赋ID和请求等级
 *   2. 调用玩家对象的LearnTalent方法学习天赋
 *   3. 发送更新后的天赋信息给客户端
 *
 * @性能注意事项:
 *   - LearnTalent内部会验证天赋前置条件、天赋点数等
 *   - SendTalentsInfoData会同步发送完整天赋树数据，数据量较大
 */
void WorldSession::HandleLearnTalentOpcode(WorldPacket& recvData)
{
    uint32 talent_id, requested_rank;
    recvData >> talent_id >> requested_rank;

    // 学习天赋（内部会验证前置条件和天赋点数）
    _player->LearnTalent(talent_id, requested_rank);
    // 发送更新后的天赋信息给客户端（false表示不是宠物天赋）
    _player->SendTalentsInfoData(false);
}

/**
 * @brief 处理批量学习预览天赋操作码
 *
 * @职责:
 *   处理玩家批量学习预览天赋的网络请求。预览天赋系统允许玩家在确认前预览天赋配置，
 *   确认后一次性学习多个天赋，避免逐个学习的繁琐操作。
 *
 * @参数:
 *   recvPacket - 接收到的网络数据包，包含：
 *                - talentsCount: 要学习的天赋数量
 *                - 多组天赋数据，每组包含：
 *                  - talentId: 天赋ID
 *                  - talentRank: 天赋等级
 *
 * @返回值: 无
 *
 * @调用时机:
 *   玩家在天赋预览界面确认学习多个天赋时，客户端发送CMSG_LEARN_PREVIEW_TALENTS消息。
 *
 * @主要流程:
 *   1. 读取要学习的天赋数量
 *   2. 循环读取每个天赋的ID和等级
 *   3. 逐个学习天赋（内部会验证前置条件）
 *   4. 发送更新后的天赋信息给客户端
 *   5. 标记数据包读取完成
 *
 * @性能注意事项:
 *   - 设置最大天赋数量限制（150）防止恶意数据包攻击
 *   - 批量学习时每个天赋单独处理，可能有性能影响
 *   - 客户端每个天赋树最多44个天赋，3个树共132个，150的上限已足够
 */
void WorldSession::HandleLearnPreviewTalents(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "CMSG_LEARN_PREVIEW_TALENTS");

    uint32 talentsCount;
    recvPacket >> talentsCount;

    uint32 talentId, talentRank;

    // 客户端每个天赋树最多44个天赋，共3个树，向上取整为150
    // 这是防止恶意数据包的安全限制
    uint32 const MaxTalentsCount = 150;

    // 循环学习每个天赋
    for (uint32 i = 0; i < talentsCount && i < MaxTalentsCount; ++i)
    {
        recvPacket >> talentId >> talentRank;

        // 学习单个天赋（内部验证前置条件和天赋点数）
        _player->LearnTalent(talentId, talentRank);
    }

    // 发送更新后的天赋信息给客户端
    _player->SendTalentsInfoData(false);

    // 标记数据包读取完成，忽略可能的额外数据
    recvPacket.rfinish();
}

/**
 * @brief 处理确认重置天赋操作码（洗点）
 *
 * @职责:
 *   处理玩家通过职业训练师重置天赋的网络请求。玩家需要支付金币来洗点，
 *   洗点费用会随着洗点次数递增。
 *
 * @参数:
 *   confirmRespecWipe - 确认重置天赋数据包，包含：
 *                       - RespecMaster: 训练师NPC的GUID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   玩家与职业训练师对话选择重置天赋选项并确认后，客户端发送MSG_TALENT_WIPE_CONFIRM消息。
 *
 * @主要流程:
 *   1. 验证训练师NPC是否存在且可交互
 *   2. 检查训练师是否可以重置玩家天赋
 *   3. 计算洗点费用（随洗点次数递增）
 *   4. 验证玩家是否有足够的金币
 *   5. 移除玩家的假死状态
 *   6. 执行天赋重置
 *   7. 扣除金币并更新洗点计数器
 *   8. 发送更新后的天赋信息给客户端
 *   9. 训练师施放洗点视觉效果法术
 *
 * @性能注意事项:
 *   - 涉及金币交易，必须严格验证以防止作弊
 *   - ResetTalents会遍历所有天赋，可能有性能影响
 *   - 洗点费用计算基于玩家洗点历史记录
 */
void WorldSession::HandleTalentWipeConfirmOpcode(WorldPackets::Talents::ConfirmRespecWipe& confirmRespecWipe)
{
    TC_LOG_DEBUG("network", "MSG_TALENT_WIPE_CONFIRM");

    // 获取并验证训练师NPC（必须存在、可交互、有训练师标志）
    Creature* trainer = GetPlayer()->GetNPCIfCanInteractWith(confirmRespecWipe.RespecMaster, UNIT_NPC_FLAG_TRAINER);
    if (!trainer)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleTalentWipeConfirmOpcode - {} not found or you can't interact with him.", confirmRespecWipe.RespecMaster);
        return;
    }

    // 检查训练师是否可以重置玩家天赋
    if (!trainer->CanResetTalents(_player, false))
        return;

    // 计算洗点费用（根据洗点次数递增）
    uint32 cost = _player->ResetTalentsCost();
    if (!_player->HasEnoughMoney(cost))
        return; // 静默返回，客户端应自行显示错误

    // 移除假死状态（假死状态下不能交互）
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 执行天赋重置
    if (!_player->ResetTalents())
    {
        // 重置失败，发送空GUID确认包
        _player->SendTalentWipeConfirm(ObjectGuid::Empty);
        return;
    }

    // 扣除金币并增加洗点计数器
    _player->ModifyMoney(-(int32)cost);
    _player->IncreaseResetTalentsCostAndCounters(cost);
    // 发送更新后的天赋信息
    _player->SendTalentsInfoData(false);

    // 训练师施放洗点视觉效果法术（法术ID: 14867）
    trainer->CastSpell(_player, 14867 /*SPELL_UNTALENT_VISUAL_EFFECT*/, true);
}

/**
 * @brief 处理遗忘技能操作码
 *
 * @职责:
 *   处理玩家遗忘专业技能的网络请求。玩家可以在技能面板中遗忘已学习的专业技能，
 *   腾出技能槽位学习新的专业技能。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含：
 *              - skillId: 要遗忘的技能ID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   玩家在技能面板中选择一个专业技能并点击遗忘按钮时，客户端发送CMSG_UNLEARN_SKILL消息。
 *
 * @主要流程:
 *   1. 从数据包中读取技能ID
 *   2. 查询技能的种族职业信息
 *   3. 验证技能是否可被遗忘（有UNLEARNABLE标志）
 *   4. 将技能值和技能上限都设置为0（实际遗忘）
 *
 * @性能注意事项:
 *   - 必须验证技能是否可遗忘，防止玩家遗忘不应该被遗忘的技能
 *   - 某些核心技能（如武器技能、护甲技能）不可遗忘
 *   - 只有带有SKILL_FLAG_UNLEARNABLE标志的专业技能才可以被遗忘
 */
void WorldSession::HandleUnlearnSkillOpcode(WorldPacket& recvData)
{
    uint32 skillId;
    recvData >> skillId;

    // 获取技能的种族职业信息（验证玩家种族职业是否可以学习此技能）
    SkillRaceClassInfoEntry const* rcEntry = GetSkillRaceClassInfo(skillId, GetPlayer()->GetRace(), GetPlayer()->GetClass());
    // 验证技能是否存在且可被遗忘
    if (!rcEntry || !(rcEntry->Flags & SKILL_FLAG_UNLEARNABLE))
        return;

    // 将技能值和上限都设为0，实现技能遗忘
    // 参数：技能ID、新值、新上限、步骤值（0表示清除）
    GetPlayer()->SetSkill(skillId, 0, 0, 0);
}
