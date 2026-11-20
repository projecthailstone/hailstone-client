#include "net/protocol/rof2/codec_dispatch.h"
#include "net/protocol/codec.h"

namespace hailstone::net::rof2::detail {
using namespace messages;
using protocol::CodecDirection;

bool implemented(GameOpcode op, CodecDirection direction) {
    // The shroud and monster mission codec does not expose raw wire bytes to GameConnection.
    switch (op) {
    case GameOpcode::ShroudRequestStats:
    case GameOpcode::ShroudSelect:
    case GameOpcode::ShroudSelectCancel:
    case GameOpcode::ShroudRequestAbility:
    case GameOpcode::ShroudRequestItem:
    case GameOpcode::MonsterMissionCatalogRequest:
    case GameOpcode::MonsterMissionStatsRequest:
    case GameOpcode::MonsterMissionAbilityRequest:
    case GameOpcode::MonsterMissionItemRequest:
    case GameOpcode::MonsterMissionSelect:
    case GameOpcode::AltStorageOpen:
    case GameOpcode::AltStorageInspect:
    case GameOpcode::AltStorageWithdraw:
        return direction == CodecDirection::Outbound;
    case GameOpcode::ShroudSelectionWindow:
    case GameOpcode::ShroudRespondStats:
    case GameOpcode::ShroudRespondAbility:
    case GameOpcode::ShroudRespondItem:
    case GameOpcode::ShroudProgress:
    case GameOpcode::ShroudProgress2:
    case GameOpcode::Shroud:
    case GameOpcode::ShroudRemove:
    case GameOpcode::MonsterMissionCatalog:
    case GameOpcode::MonsterMissionStats:
    case GameOpcode::MonsterMissionAbility:
    case GameOpcode::MonsterMissionItem:
    case GameOpcode::MonsterMissionAssignment:
    case GameOpcode::MonsterMissionResult:
    case GameOpcode::AltStorage:
    case GameOpcode::AltStorageList:
    case GameOpcode::AltStorageNoItems:
        return direction == CodecDirection::Inbound;
    default: break;
    }
    // Guild banner opcodes are available only in their actual direction.
    switch (op) {
    case GameOpcode::GuildBannerOpen:
    case GameOpcode::GuildBannerKitRequest:
    case GameOpcode::GuildBannerStatusRequest:
    case GameOpcode::GuildBannerSave:
    case GameOpcode::GuildBannerPlant:
    case GameOpcode::GuildBannerRemove:
        return direction == CodecDirection::Outbound;
    case GameOpcode::GuildBannerKitList:
    case GameOpcode::GuildBannerStatus:
    case GameOpcode::GuildBannerModificationToken:
    case GameOpcode::GuildBannerAppearance:
        return direction == CodecDirection::Inbound;
    default: break;
    }
    if (direction == CodecDirection::Outbound) {
        switch (op) {
    case GameOpcode::DuelDecline: case GameOpcode::GuildDelete: case GameOpcode::GMSummon:
    case GameOpcode::CorpseDrag: case GameOpcode::CorpseDrop: case GameOpcode::Shielding:
    case GameOpcode::ClearSurname: case GameOpcode::KeyRing: case GameOpcode::ResetAA:
    case GameOpcode::PopupResponse: case GameOpcode::PotionBelt:
    case GameOpcode::Bandolier:
        return true;
        case GameOpcode::ReLeave: case GameOpcode::UptimeRequest: case GameOpcode::ServerTransferRequest: case GameOpcode::Hide:
        case GameOpcode::InspectRequest: case GameOpcode::InspectMessageUpdate:
        case GameOpcode::ReadBook: case GameOpcode::BookButton: case GameOpcode::LDoNInspect:
        case GameOpcode::Track: case GameOpcode::TrackTarget: case GameOpcode::FindPersonRequest: case GameOpcode::RespawnWindow: case GameOpcode::FloatListThing:
        case GameOpcode::SharedTaskInviteResponse:
        case GameOpcode::DzAddPlayer: case GameOpcode::DzRemovePlayer: case GameOpcode::DzSwapPlayer: case GameOpcode::DzMakeLeader:
        case GameOpcode::DzQuit: case GameOpcode::DzListTimers: case GameOpcode::DzPlayerList:
        case GameOpcode::DzChooseZoneReply: case GameOpcode::DzExpeditionInviteResponse:
        case GameOpcode::AdventureInfoRequest: case GameOpcode::AdventureRequest: case GameOpcode::LDoNButton:
        case GameOpcode::LeaveAdventure: case GameOpcode::AdventureStatsRequest: case GameOpcode::AdventureLeaderboardRequest:
        case GameOpcode::WhoAllRequest: case GameOpcode::FriendsWho: case GameOpcode::RandomReq:
        case GameOpcode::Consent: case GameOpcode::ConsentDeny: case GameOpcode::Emote: case GameOpcode::Assist:
        case GameOpcode::AcceptNewTask: case GameOpcode::SharedTaskAcceptNew: case GameOpcode::CancelTask:
        case GameOpcode::TaskHistoryRequest: case GameOpcode::SharedTaskAddPlayer: case GameOpcode::SharedTaskRemovePlayer:
        case GameOpcode::SharedTaskMakeLeader: case GameOpcode::SharedTaskPlayerList: case GameOpcode::SharedTaskQuit: case GameOpcode::TaskTimers:
        case GameOpcode::GuildInvite: case GameOpcode::GuildRemove: case GameOpcode::GuildInviteAccept:
        case GameOpcode::SetGuildMOTD: case GameOpcode::GuildPromote: case GameOpcode::GuildWar: case GameOpcode::GuildPeace:
        case GameOpcode::GuildLeader: case GameOpcode::GuildPublicNote: case GameOpcode::GuildManageBanker:
        case GameOpcode::GuildUpdate: case GameOpcode::GetGuildMOTD:
        case GameOpcode::LFGCommand: case GameOpcode::LFPCommand:
        case GameOpcode::LFGuild: case GameOpcode::LFGGetMatchesRequest: case GameOpcode::LFPGetMatchesRequest:
        case GameOpcode::FellowshipRequest: case GameOpcode::FellowshipCampRequest:
        case GameOpcode::RaidInvite:
        case GameOpcode::LeadershipExpToggle: case GameOpcode::PurchaseLeadershipAA: case GameOpcode::DoGroupLeadershipAbility:
        case GameOpcode::DelegateAbility: case GameOpcode::RaidDelegateAbility:
        case GameOpcode::GroupInvite: case GameOpcode::GroupInvite2:
        case GameOpcode::GroupFollow: case GameOpcode::GroupFollow2:
        case GameOpcode::CancelInvite: case GameOpcode::GroupDisband:
        case GameOpcode::GroupMakeLeader: case GameOpcode::GroupMentor: case GameOpcode::GroupRoles:
        case GameOpcode::AchievementReward: case GameOpcode::RewardSelection:
        case GameOpcode::AchievementWindowRequest: case GameOpcode::AchievementCompare:
        case GameOpcode::AchievementLinkRequest: case GameOpcode::AchievementRewardView:
        case GameOpcode::GMTraining: case GameOpcode::GMTrainSkill: case GameOpcode::GMEndTraining:
        case GameOpcode::OpenTributeMaster: case GameOpcode::OpenGuildTributeMaster:
        case GameOpcode::SelectTribute: case GameOpcode::TributeUpdate: case GameOpcode::TributeToggle:
        case GameOpcode::TributeItem: case GameOpcode::TributeMoney: case GameOpcode::GuildSelectTribute:
        case GameOpcode::GuildSaveActiveTributes: case GameOpcode::GuildModifyBenefits:
        case GameOpcode::GuildTributeToggleReq: case GameOpcode::GuildOptInOut: case GameOpcode::GuildTrophyOptInOut:
        case GameOpcode::GuildTributeDonateItem: case GameOpcode::GuildTributeDonatePlat:
        case GameOpcode::SendAATable: case GameOpcode::UpdateAA: case GameOpcode::AAAction:
        case GameOpcode::SendTributes:         case GameOpcode::RequestGuildTributes:
        case GameOpcode::Camp:         case GameOpcode::Logout:         case GameOpcode::Save:         case GameOpcode::SaveOnZoneReq:         case GameOpcode::DeleteSpawn:
        case GameOpcode::Jump:         case GameOpcode::SafeFallSuccess:         case GameOpcode::EnvDamage:         case GameOpcode::SetRunMode:         case GameOpcode::AckPacket:
        case GameOpcode::GroundSpawn:         case GameOpcode::ClickDoor:         case GameOpcode::ClickObject:         case GameOpcode::ClickObjectAction:
        case GameOpcode::TargetMouse:         case GameOpcode::TargetCommand:         case GameOpcode::XTargetRequest:         case GameOpcode::XTargetAutoAddHaters:
        case GameOpcode::AggroMeterLockTarget:
        case GameOpcode::AutoAttack:         case GameOpcode::AutoAttack2:         case GameOpcode::AutoFire:         case GameOpcode::CombatAbility:
        case GameOpcode::CastSpell:         case GameOpcode::ManaChange:         case GameOpcode::Animation:
        case GameOpcode::MemorizeSpell:         case GameOpcode::BuffRemoveRequest:         case GameOpcode::Buff:         case GameOpcode::BlockedBuffs:         case GameOpcode::RemoveBlockedBuffs:         case GameOpcode::ClearBlockedBuffs:         case GameOpcode::LoadSpellSet:
        case GameOpcode::PetCommands:
        case GameOpcode::ControlBoat:
        case GameOpcode::YellForHelp:
        case GameOpcode::Rewind:
        case GameOpcode::MoveItem: case GameOpcode::MoveMultipleItems: case GameOpcode::DeleteItem: case GameOpcode::LootRequest: case GameOpcode::EndLootRequest: case GameOpcode::LootItem: case GameOpcode::ItemLinkClick: case GameOpcode::ItemLinkResponse: case GameOpcode::ItemVerifyRequest: case GameOpcode::ItemPreviewRequest: case GameOpcode::ApplyPoison: case GameOpcode::Consume: case GameOpcode::EvolveItem: case GameOpcode::AugmentItem: case GameOpcode::AugmentInfo:
        case GameOpcode::MoveCoin:
        case GameOpcode::CursorCoinSplit:
        case GameOpcode::TradeRequest: case GameOpcode::TradeRequestAck: case GameOpcode::TradeAcceptClick: case GameOpcode::TradeBusy: case GameOpcode::CancelTrade:
        case GameOpcode::ShopRequest: case GameOpcode::ShopPlayerBuy: case GameOpcode::ShopPlayerSell: case GameOpcode::ShopEnd: case GameOpcode::AltCurrencyReclaim: case GameOpcode::AltCurrencyMerchantRequest: case GameOpcode::AltCurrencyPurchase: case GameOpcode::AltCurrencySellSelection: case GameOpcode::AltCurrencySell: case GameOpcode::AdventureMerchantRequest: case GameOpcode::AdventureMerchantPurchase: case GameOpcode::AdventureMerchantSell:
        case GameOpcode::TradeSkillCombine: case GameOpcode::RecipesFavorite: case GameOpcode::RecipesSearch: case GameOpcode::RecipeDetails: case GameOpcode::RecipeAutoCombine:
        case GameOpcode::Split:
        case GameOpcode::Trader: case GameOpcode::TraderShop: case GameOpcode::FindMarketSpawn: case GameOpcode::Barter: case GameOpcode::BuyerItems:
        case GameOpcode::ShopRetrieveParcel: case GameOpcode::ShopSendParcel:
        case GameOpcode::GuildBank:
        case GameOpcode::MarketplaceRequestItems: case GameOpcode::MarketplaceRequestFunding: case GameOpcode::MarketplaceReady: case GameOpcode::MarketplaceHello: case GameOpcode::MarketplacePurchase: case GameOpcode::MarketplacePreviewRequest: case GameOpcode::MarketplaceZip:
        case GameOpcode::ReSearch: case GameOpcode::ReTravel: case GameOpcode::RePlotSelect: case GameOpcode::RePurchase: case GameOpcode::ReSell: case GameOpcode::ReTransfer: case GameOpcode::ReOptOut: case GameOpcode::ReEscrow: case GameOpcode::ReAccessSave: case GameOpcode::ReToStorage: case GameOpcode::ReToInventory: case GameOpcode::ReMenu5: case GameOpcode::ReMenu8: case GameOpcode::ReMenu9: case GameOpcode::ReMenu13: case GameOpcode::ReStoreAll: case GameOpcode::RePackCrate: case GameOpcode::ReInspect: case GameOpcode::ReContextMenu: case GameOpcode::RePlace: case GameOpcode::RePlaceConfirm: case GameOpcode::ReOffer:
        case GameOpcode::RequestTitles: case GameOpcode::SetTitle: case GameOpcode::RequestDuel: case GameOpcode::DuelAccept: case GameOpcode::Surname:
        case GameOpcode::SetServerFilter: case GameOpcode::GuildDemote: case GameOpcode::GuildCreate:
        case GameOpcode::GuildStatus: case GameOpcode::RaidNote: case GameOpcode::RaidMotd:
        case GameOpcode::SwapSpell:
        case GameOpcode::Bug: case GameOpcode::Consider: case GameOpcode::PVPLeaderBoardRequest:
        case GameOpcode::VetClaimRequest: case GameOpcode::Dye: case GameOpcode::UpdateAura:
        case GameOpcode::ClearNPCMarks: case GameOpcode::RaidClearNPCMarks:
        case GameOpcode::LDoNOpen: case GameOpcode::SetStartCity: case GameOpcode::HideCorpse:
        case GameOpcode::TGB: case GameOpcode::Petition: case GameOpcode::Report: case GameOpcode::Feedback:
        case GameOpcode::PlayerStateAdd: case GameOpcode::PlayerStateRemove: case GameOpcode::SetFace:
        case GameOpcode::SendLoginInfo: case GameOpcode::EnterWorld:
        case GameOpcode::World_Client_CRC1: case GameOpcode::World_Client_CRC2: case GameOpcode::World_Client_CRC3:
        case GameOpcode::WorldClientReady: case GameOpcode::CharacterCreateRequest: case GameOpcode::WorldComplete:
        case GameOpcode::CharacterCreate: case GameOpcode::DeleteCharacter:
        case GameOpcode::RandomNameGenerator: case GameOpcode::ApproveName:
        case GameOpcode::ZoneEntry: case GameOpcode::ReqClientSpawn:
        case GameOpcode::SendExpZonein: case GameOpcode::WorldObjectsSent:
        case GameOpcode::ClientReady: case GameOpcode::ClientUpdate:
        case GameOpcode::ChannelMessage: case GameOpcode::ZoneChange:
        case GameOpcode::SpawnAppearance: case GameOpcode::WearChange:
        case GameOpcode::TimeOfDay: case GameOpcode::Weather:
        case GameOpcode::Damage:
            return true;
        default: return false;
        }
    }
    switch (op) {
    // Server-to-client title and level notifications: these are decoded
    // below, never serialized as client commands.
    case GameOpcode::SendTitleList: case GameOpcode::SetTitleReply:
    case GameOpcode::NewTitlesAvailable: case GameOpcode::OnLevelMessage:
    // The same request/response names have valid traffic in both directions.
    case GameOpcode::RequestDuel: case GameOpcode::DuelAccept:
    case GameOpcode::Surname: case GameOpcode::Bandolier:
    case GameOpcode::SharedTaskInvite: case GameOpcode::SendFindableNPCs: case GameOpcode::SpecialMesg: case GameOpcode::SimpleMessage: case GameOpcode::FormattedMessage:
    case GameOpcode::PVPStats: case GameOpcode::PVPLeaderBoardReply: case GameOpcode::CrystalCountUpdate:
    case GameOpcode::VetRewardsAvaliable: case GameOpcode::VetClaimReply: case GameOpcode::UpdateAura:
    case GameOpcode::Begging: case GameOpcode::PickPocket: case GameOpcode::Bind_Wound:
    case GameOpcode::CancelSneakHide:
    case GameOpcode::InspectAnswer: case GameOpcode::ReadBook:
    case GameOpcode::Track: case GameOpcode::FindPersonReply:
    case GameOpcode::DzExpeditionInfo: case GameOpcode::DzExpeditionInvite: case GameOpcode::DzExpeditionLockoutTimers:
    case GameOpcode::DzMemberList: case GameOpcode::DzMemberListStatus: case GameOpcode::DzMemberListName:
    case GameOpcode::DzSetLeaderName: case GameOpcode::DzExpeditionEndsWarning: case GameOpcode::DzChooseZone: case GameOpcode::DzCompass:
    case GameOpcode::AdventureInfo: case GameOpcode::AdventureDetails: case GameOpcode::AdventureData:
    case GameOpcode::AdventureUpdate: case GameOpcode::AdventureFinish: case GameOpcode::AdventurePointsUpdate:
    case GameOpcode::AdventureStatsReply: case GameOpcode::AdventureLeaderboardReply:
    case GameOpcode::TaskDescription: case GameOpcode::TaskActivity: case GameOpcode::CompletedTasks:
    case GameOpcode::TaskSelectWindow: case GameOpcode::SharedTaskSelectWindow: case GameOpcode::TaskHistoryReply:
    case GameOpcode::TaskActivityComplete: case GameOpcode::CancelTask: case GameOpcode::SharedTaskMemberList:
    case GameOpcode::SharedTaskMemberChange: case GameOpcode::TaskRequestTimer:
    case GameOpcode::Emote: case GameOpcode::WhoAllResponse: case GameOpcode::RandomReply: case GameOpcode::Assist:
    case GameOpcode::GuildMOTD: case GameOpcode::GetGuildMOTDReply: case GameOpcode::GuildMemberList:
    case GameOpcode::GuildMemberAdd: case GameOpcode::GuildMemberDelete: case GameOpcode::GuildMemberLevel:
    case GameOpcode::GuildMemberPublicNote:
    // The alias resolver emits the canonical rank/member operations; the
    // alternative wire names are not separately advertised as decoded events.
    case GameOpcode::GuildMemberRankAltBanker:
    case GameOpcode::GuildMemberUpdate: case GameOpcode::GuildUpdate:
    case GameOpcode::GuildInvite: case GameOpcode::ConsentResponse: case GameOpcode::DenyResponse:
    case GameOpcode::LFGuild: case GameOpcode::LFGGetMatchesResponse: case GameOpcode::LFPGetMatchesResponse:
    case GameOpcode::FellowshipRoster: case GameOpcode::FellowshipKitList:
    case GameOpcode::FellowshipVitality: case GameOpcode::FellowshipInvite:
    case GameOpcode::RaidUpdate:
    case GameOpcode::GroupLeadershipAAUpdate: case GameOpcode::DelegateAbility: case GameOpcode::RaidDelegateAbility:
    case GameOpcode::SetGroupTarget: case GameOpcode::MarkNPC: case GameOpcode::MarkRaidNPC:
    case GameOpcode::ClearNPCMarks: case GameOpcode::RaidClearNPCMarks: case GameOpcode::InspectBuffs:
    case GameOpcode::GroupInvite: case GameOpcode::GroupInvite2:
    case GameOpcode::GroupFollow: case GameOpcode::GroupFollow2: case GameOpcode::CancelInvite:
    case GameOpcode::GroupUpdate: case GameOpcode::GroupUpdateB: case GameOpcode::GroupRoles:
    case GameOpcode::GroupAcknowledge: case GameOpcode::GroupDisbandYou:
    case GameOpcode::GroupDisbandOther: case GameOpcode::GroupLeaderChange: case GameOpcode::GroupDelete:
    case GameOpcode::UpdateLeadershipAA: case GameOpcode::LeadershipExpUpdate:
    case GameOpcode::MobManaUpdate: case GameOpcode::MobEnduranceUpdate:
    case GameOpcode::AchievementReward: case GameOpcode::RewardSelection:
    case GameOpcode::AchievementDefinitions: case GameOpcode::AchievementPrimary: case GameOpcode::AchievementProgress:
    case GameOpcode::AchievementSnapshot: case GameOpcode::AchievementInspect: case GameOpcode::AchievementEarned:
    case GameOpcode::AchievementWindow: case GameOpcode::AchievementGuild:
    case GameOpcode::SkillUpdate: case GameOpcode::GMTraining: case GameOpcode::GMTrainSkillConfirm:
    case GameOpcode::TributeInfo: case GameOpcode::TributeUpdate: case GameOpcode::TributeTimer:
    case GameOpcode::TributePointUpdate: case GameOpcode::SelectTribute: case GameOpcode::OpenTributeMaster:
    case GameOpcode::TributeItem: case GameOpcode::TributeMoney: case GameOpcode::SendGuildTributes:
    case GameOpcode::GuildSelectTribute: case GameOpcode::GuildSendActiveTributes: case GameOpcode::GuildTributeToggleReply:
    case GameOpcode::GuildTributeFavorAndTimer: case GameOpcode::GuildOptInOut: case GameOpcode::GuildTributeDonateItem:
    case GameOpcode::GuildTributeDonatePlat: case GameOpcode::OpenGuildTributeMaster: case GameOpcode::GuildModifyBenefits:
    case GameOpcode::SendAATable: case GameOpcode::RespondAA: case GameOpcode::AAExpUpdate: case GameOpcode::AAAction: case GameOpcode::ClearAA:
    case GameOpcode::DeleteSpawn:     case GameOpcode::RequestClientZoneChange:
    case GameOpcode::SetFace:     case GameOpcode::Illusion:     case GameOpcode::ChangeSize:     case GameOpcode::PlayerStateAdd:     case GameOpcode::PlayerStateRemove:
    case GameOpcode::SpawnDoor:     case GameOpcode::MoveDoor:     case GameOpcode::RemoveAllDoors:     case GameOpcode::GroundSpawn:     case GameOpcode::ClickObject:     case GameOpcode::ClickObjectAction:     case GameOpcode::ClearObject:     case GameOpcode::SendZonepoints:
    case GameOpcode::TargetCommand:     case GameOpcode::XTargetResponse:
    case GameOpcode::AggroMeterTargetInfo:     case GameOpcode::AggroMeterUpdate:
    case GameOpcode::MobRename:     case GameOpcode::MobHealth:     case GameOpcode::HPUpdate:     case GameOpcode::ManaChange:     case GameOpcode::ManaUpdate:     case GameOpcode::EnduranceUpdate:     case GameOpcode::ExpUpdate:     case GameOpcode::LevelUpdate:     case GameOpcode::Consider:     case GameOpcode::Damage:     case GameOpcode::Death:     case GameOpcode::SomeItemPacketMaybe:     case GameOpcode::Animation:     case GameOpcode::ZonePlayerToBind:     case GameOpcode::RespawnWindow:
    case GameOpcode::BeginCast:     case GameOpcode::InterruptCast:     case GameOpcode::Action:
    case GameOpcode::RestState:     case GameOpcode::MemorizeSpell:     case GameOpcode::Buff:     case GameOpcode::BuffCreate:     case GameOpcode::TargetBuffs: case GameOpcode::PetBuffWindow:     case GameOpcode::BlockedBuffs:     case GameOpcode::RemoveBlockedBuffs:     case GameOpcode::ClearBlockedBuffs:     case GameOpcode::SpellEffect:     case GameOpcode::AddNimbusEffect:     case GameOpcode::RemoveNimbusEffect:     case GameOpcode::DeleteSpell:     case GameOpcode::SwapSpell:     case GameOpcode::ColoredText:     case GameOpcode::Stamina:     case GameOpcode::LinkedReuse:     case GameOpcode::DisciplineTimer:
    case GameOpcode::PetCommandState: case GameOpcode::PetHoTT: case GameOpcode::TargetHoTT: case GameOpcode::Charm:
    case GameOpcode::ControlBoat:
    case GameOpcode::Stun: case GameOpcode::Untargetable: case GameOpcode::YellForHelp: case GameOpcode::Weblink: case GameOpcode::DisciplineUpdate: case GameOpcode::ItemRecastDelay:
    case GameOpcode::CharInventory: case GameOpcode::ItemPacket: case GameOpcode::ItemLinkResponse: case GameOpcode::MoveItem: case GameOpcode::DeleteItem: case GameOpcode::DeleteCharge: case GameOpcode::MoneyOnCorpse: case GameOpcode::LootRequest: case GameOpcode::LootItem: case GameOpcode::LootComplete: case GameOpcode::MoneyUpdate: case GameOpcode::Sound: case GameOpcode::CashReward: case GameOpcode::ItemVerifyReply: case GameOpcode::ApplyPoison: case GameOpcode::ItemPreviewRequest: case GameOpcode::AugmentInfo: case GameOpcode::EvolveItem:
    case GameOpcode::TradeRequest: case GameOpcode::TradeRequestAck: case GameOpcode::TradeAcceptClick: case GameOpcode::TradeCoins: case GameOpcode::TradeBusy: case GameOpcode::CancelTrade: case GameOpcode::FinishTrade: case GameOpcode::FinishWindow: case GameOpcode::FinishWindow2:
    case GameOpcode::ShopRequest: case GameOpcode::ShopPlayerBuy: case GameOpcode::ShopPlayerSell: case GameOpcode::ShopEnd: case GameOpcode::ShopEndConfirm: case GameOpcode::ShopDelItem: case GameOpcode::AltCurrency: case GameOpcode::AltCurrencyMerchantReply: case GameOpcode::AltCurrencySellSelection: case GameOpcode::AltCurrencySell: case GameOpcode::AdventureMerchantResponse: case GameOpcode::AdventureMerchantSell:
    case GameOpcode::TradeSkillCombine: case GameOpcode::RecipeReply: case GameOpcode::RecipeDetails: case GameOpcode::RecipeAutoCombine:
    case GameOpcode::Trader: case GameOpcode::BecomeTrader: case GameOpcode::TraderShop: case GameOpcode::TraderBulkSend: case GameOpcode::BazaarSearch: case GameOpcode::TraderDelItem: case GameOpcode::Barter: case GameOpcode::BuyerItems:
    case GameOpcode::ShopDeleteParcel: case GameOpcode::ShopSendParcel: case GameOpcode::ShopRetrieveParcel: case GameOpcode::ShopParcelIcon:
    case GameOpcode::GuildBank: case GameOpcode::GuildBankItemList:
    case GameOpcode::MarketplaceItems: case GameOpcode::MarketplaceFunding: case GameOpcode::MarketplaceStates: case GameOpcode::MarketplaceWallet: case GameOpcode::MarketplacePurchaseResult: case GameOpcode::MarketplaceUnavailable: case GameOpcode::MarketplacePreview: case GameOpcode::MarketplaceZipStatus:
    case GameOpcode::ReNop1: case GameOpcode::ReNop2: case GameOpcode::ReItemSummaryUpdate: case GameOpcode::RePlaceStop: case GameOpcode::ReCatalog: case GameOpcode::ReRecord: case GameOpcode::ReSetCurrent: case GameOpcode::ReRemove: case GameOpcode::ReAccess: case GameOpcode::ReAccessUpdate: case GameOpcode::ReItemSummary: case GameOpcode::ReItemRows: case GameOpcode::ReItemRowAdd: case GameOpcode::ReItemSummaryRename: case GameOpcode::ReItemRemoved: case GameOpcode::RePlacedItemRemove: case GameOpcode::ReItemSummaryForget: case GameOpcode::ReResult: case GameOpcode::ReSellResult: case GameOpcode::ReTransferResult: case GameOpcode::RePlotSearchOpen: case GameOpcode::ReSearch: case GameOpcode::ReEscrow: case GameOpcode::ReOwnedA: case GameOpcode::ReOwnedB: case GameOpcode::ReOwnedC: case GameOpcode::ReOwnedAToggle: case GameOpcode::ReOwnedBToggle: case GameOpcode::ReNpcItem: case GameOpcode::ReContextMenu: case GameOpcode::ReInspect: case GameOpcode::ReOffer: case GameOpcode::RePlacedItem: case GameOpcode::ReAreas: case GameOpcode::RePlace: case GameOpcode::RePurchase:
    case GameOpcode::ApproveWorld: case GameOpcode::PostEnterWorld:
    case GameOpcode::SendMaxCharacters: case GameOpcode::SetChatServer:
    case GameOpcode::LogServer: case GameOpcode::SendCharInfo:
    case GameOpcode::SendMembership: case GameOpcode::SendMembershipDetails:
    case GameOpcode::GuildsList: case GameOpcode::ExpansionInfo:
    case GameOpcode::ApproveName: case GameOpcode::CharacterCreateRequest:
    case GameOpcode::RandomNameGenerator: case GameOpcode::MOTD:
    case GameOpcode::EnterWorld: case GameOpcode::ZoneUnavail:
    case GameOpcode::SetChatServer2: case GameOpcode::ZoneServerInfo:
    case GameOpcode::ZoneEntry: case GameOpcode::NewSpawn:
    case GameOpcode::PlayerProfile: case GameOpcode::NewZone:
    case GameOpcode::SendExpZonein: case GameOpcode::SendAAStats:
    case GameOpcode::ClientReady: case GameOpcode::WorldObjectsSent:
    case GameOpcode::TimeOfDay: case GameOpcode::Weather:
    case GameOpcode::ClientUpdate: case GameOpcode::NPCMoveUpdate:
    case GameOpcode::SpawnAppearance: case GameOpcode::WearChange:
    case GameOpcode::ChannelMessage: case GameOpcode::ZoneChange:
        return true;
    default: return false;
    }
}


} // namespace hailstone::net::rof2::detail
