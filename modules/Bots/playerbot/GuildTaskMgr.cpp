#include "../botpch.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "GuildTaskMgr.h"

#include "GuildMgr.h"
#include "DatabaseEnv.h"
#include "Mail.h"
#include "PlayerbotAI.h"

#include "RandomItemMgr.h"

#include "Group.h"

char * strstri(const char* str1, const char* str2);

enum GuildTaskType
{
	GUILD_TASK_TYPE_NONE = 0,
	GUILD_TASK_TYPE_ITEM = 1,
	GUILD_TASK_TYPE_KILL = 2
};

GuildTaskMgr::GuildTaskMgr()
{
}

GuildTaskMgr::~GuildTaskMgr()
{
}

void GuildTaskMgr::Update(Player* player, Player* guildMaster)
{
	if (!sPlayerbotAIConfig.guildTaskEnabled)
		return;

	uint32 guildId = guildMaster->GetGuildId();
	if (!guildId || !guildMaster->GetPlayerbotAI() || !guildMaster->GetGuildId())
		return;

	if (!player->IsFriendlyTo(guildMaster))
		return;

	Guild *guild = sGuildMgr->GetGuildById(guildMaster->GetGuildId());
	DenyReason reason = PLAYERBOT_DENY_NONE;
	PlayerbotSecurityLevel secLevel = guildMaster->GetPlayerbotAI()->GetSecurity()->LevelFor(player, &reason);
	if (secLevel == PLAYERBOT_SECURITY_DENY_ALL || (secLevel == PLAYERBOT_SECURITY_TALK && reason != PLAYERBOT_DENY_FAR))
	{
		sLog->outString("%s / %s: skipping guild task update - not enough security level, reason = %u",
			guild->GetName().c_str(), player->GetName().c_str(), reason);
		return;
	}

	uint32 owner = (uint32)player->GetGUIDLow();

	uint32 activeTask = GetTaskValue(owner, guildId, "activeTask");
	if (!activeTask)
	{
		SetTaskValue(owner, guildId, "killTask", 0, 0);
		SetTaskValue(owner, guildId, "itemTask", 0, 0);
		SetTaskValue(owner, guildId, "itemCount", 0, 0);
		SetTaskValue(owner, guildId, "killTask", 0, 0);
		SetTaskValue(owner, guildId, "killCount", 0, 0);
		SetTaskValue(owner, guildId, "payment", 0, 0);
		SetTaskValue(owner, guildId, "thanks", 1, 2 * sPlayerbotAIConfig.maxGuildTaskChangeTime);
		SetTaskValue(owner, guildId, "reward", 1, 2 * sPlayerbotAIConfig.maxGuildTaskChangeTime);

		uint32 task = CreateTask(owner, guildId);

		if (task == GUILD_TASK_TYPE_NONE)
		{
			sLog->outError("%s / %s: error creating guild task",
				guild->GetName().c_str(), player->GetName().c_str());
		}

		uint32 time = urand(sPlayerbotAIConfig.minGuildTaskChangeTime, sPlayerbotAIConfig.maxGuildTaskChangeTime);
		SetTaskValue(owner, guildId, "activeTask", task, time);
		SetTaskValue(owner, guildId, "advertisement", 1,
			urand(sPlayerbotAIConfig.minGuildTaskAdvertisementTime, sPlayerbotAIConfig.maxGuildTaskAdvertisementTime));

		sLog->outString("%s / %s: guild task %u is set for %u secs",
			guild->GetName().c_str(), player->GetName().c_str(),
			task, time);
		return;
	}

	uint32 advertisement = GetTaskValue(owner, guildId, "advertisement");
	if (!advertisement)
	{
		sLog->outString("%s / %s: sending advertisement",
			guild->GetName().c_str(), player->GetName().c_str());
		if (SendAdvertisement(owner, guildId))
		{
			SetTaskValue(owner, guildId, "advertisement", 1,
				urand(sPlayerbotAIConfig.minGuildTaskAdvertisementTime, sPlayerbotAIConfig.maxGuildTaskAdvertisementTime));
		}
		else
		{
			sLog->outError("%s / %s: error sending advertisement",
				guild->GetName().c_str(), player->GetName().c_str());
		}
	}

	uint32 thanks = GetTaskValue(owner, guildId, "thanks");
	if (!thanks)
	{
		sLog->outString("%s / %s: sending thanks",
			guild->GetName().c_str(), player->GetName().c_str());
		if (SendThanks(owner, guildId))
		{
			SetTaskValue(owner, guildId, "thanks", 1, 2 * sPlayerbotAIConfig.maxGuildTaskChangeTime);
			SetTaskValue(owner, guildId, "payment", 0, 0);
		}
		else
		{
			sLog->outError("%s / %s: error sending thanks",
				guild->GetName().c_str(), player->GetName().c_str());
		}
	}

	uint32 reward = GetTaskValue(owner, guildId, "reward");
	if (!reward)
	{
		sLog->outString("%s / %s: sending reward",
			guild->GetName().c_str(), player->GetName().c_str());
		if (Reward(owner, guildId))
		{
			SetTaskValue(owner, guildId, "reward", 1, 2 * sPlayerbotAIConfig.maxGuildTaskChangeTime);
			SetTaskValue(owner, guildId, "payment", 0, 0);
		}
		else
		{
			sLog->outError("%s / %s: error sending reward",
				guild->GetName().c_str(), player->GetName().c_str());
		}
	}
}

uint32 GuildTaskMgr::CreateTask(uint32 owner, uint32 guildId)
{
	switch (urand(0, 1))
	{
	case 0:
		CreateItemTask(owner, guildId);
		return GUILD_TASK_TYPE_ITEM;
	default:
		CreateKillTask(owner, guildId);
		return GUILD_TASK_TYPE_KILL;
	}
}

bool GuildTaskMgr::CreateItemTask(uint32 owner, uint32 guildId)
{
	Player* player = ObjectAccessor::FindPlayer((uint64)owner);
	if (!player)
		return false;

	uint32 itemId = sRandomItemMgr.GetRandomItem(RANDOM_ITEM_GUILD_TASK);
	if (!itemId)
	{
		sLog->outError("%s / %s: no items avaible for item task",
			sGuildMgr->GetGuildById(guildId)->GetName().c_str(), player->GetName().c_str());
		return false;
	}

	uint32 count = GetMaxItemTaskCount(itemId);

	sLog->outString("%s / %s: item task %u (x%d)",
		sGuildMgr->GetGuildById(guildId)->GetName().c_str(), player->GetName().c_str(),
		itemId, count);

	SetTaskValue(owner, guildId, "itemCount", count, sPlayerbotAIConfig.maxGuildTaskChangeTime);
	SetTaskValue(owner, guildId, "itemTask", itemId, sPlayerbotAIConfig.maxGuildTaskChangeTime);
	return true;
}

bool GuildTaskMgr::CreateKillTask(uint32 owner, uint32 guildId)
{
	Player* player = ObjectAccessor::FindPlayer((uint64)owner);
	if (!player)
		return false;

	uint32 rank = !urand(0, 2) ? CREATURE_ELITE_RAREELITE : CREATURE_ELITE_RARE;
	vector<uint32> ids;
	CreatureTemplateContainer const* creatureTemplateContainer = sObjectMgr->GetCreatureTemplates();
	for (CreatureTemplateContainer::const_iterator i = creatureTemplateContainer->begin(); i != creatureTemplateContainer->end(); ++i)
	{
		CreatureTemplate const& co = i->second;

		if (co.rank != rank)
			continue;

		if (co.maxlevel > player->getLevel() + 4 || co.minlevel < player->getLevel() - 3)
			continue;

		const char* UNUSED = "UNUSED";
		if (strstr(co.Name.c_str(), UNUSED))
			continue;

		ids.push_back(co.Entry);
	}

	if (ids.empty())
	{
		sLog->outError("%s / %s: no rare creatures available for kill task",
			sGuildMgr->GetGuildById(guildId)->GetName().c_str(), player->GetName().c_str());
		return false;
	}

	uint32 index = urand(0, ids.size() - 1);
	uint32 creatureId = ids[index];

	sLog->outString("%s / %s: kill task %u",
		sGuildMgr->GetGuildById(guildId)->GetName().c_str(), player->GetName().c_str(),
		creatureId);

	SetTaskValue(owner, guildId, "killTask", creatureId, sPlayerbotAIConfig.maxGuildTaskChangeTime);
	return true;
}

bool GuildTaskMgr::SendAdvertisement(uint32 owner, uint32 guildId)
{
	Guild *guild = sGuildMgr->GetGuildById(guildId);
	if (!guild)
		return false;

	Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
	if (!leader)
		return false;

	uint32 validIn;
	GetTaskValue(owner, guildId, "activeTask", &validIn);

	uint32 itemTask = GetTaskValue(owner, guildId, "itemTask");
	if (itemTask)
		return SendItemAdvertisement(itemTask, owner, guildId, validIn);

	uint32 killTask = GetTaskValue(owner, guildId, "killTask");
	if (killTask)
		return SendKillAdvertisement(killTask, owner, guildId, validIn);

	return false;
}

string formatTime(uint32 secs)
{
	ostringstream out;
	if (secs < 3600)
	{
		out << secs / 60 << " min";
	}
	else if (secs < 7200)
	{
		out << "1 hr " << (secs - 3600) / 60 << " min";
	}
	else if (secs < 3600 * 24)
	{
		out << secs / 3600 << " hr";
	}
	else
	{
		out << secs / 3600 / 24 << " days";
	}

	return out.str();
}

string formatDateTime(uint32 secs)
{
	time_t rawtime = time(0) + secs;
	tm* timeinfo = localtime(&rawtime);

	char buffer[256];
	strftime(buffer, sizeof(buffer), "%b %d, %H:%M", timeinfo);
	return string(buffer);
}

string GetHelloText(uint32 owner)
{
	ostringstream body;
	body << "Hello";
	string playerName;
	sObjectMgr->GetPlayerNameByGUID(owner, playerName);
	if (!playerName.empty()) body << ", " << playerName;
	body << ",\n\n";
	return body.str();
}

bool GuildTaskMgr::SendItemAdvertisement(uint32 itemId, uint32 owner, uint32 guildId, uint32 validIn)
{
	Guild *guild = sGuildMgr->GetGuildById(guildId);
	Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());

	ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
	if (!proto)
		return false;

	ostringstream body;
	body << GetHelloText(owner);
	body << "We are in a great need of " << proto->Name1 << ". If you could sell us ";
	uint32 count = GetTaskValue(owner, guildId, "itemCount");
	if (count > 1)
		body << "at least " << count << " of them ";
	else
		body << "some ";
	body << "we'd really appreciate that and pay a high price.\n\n";
	body << "The task will expire at " << formatDateTime(validIn) << "\n";
	body << "\n";
	body << "Best Regards,\n";
	body << guild->GetName() << "\n";
	body << leader->GetName() << "\n";

	SQLTransaction trans = CharacterDatabase.BeginTransaction();
	ostringstream subject;
	subject << "Guild Task: " << proto->Name1;
	MailDraft(subject.str(), body.str()).SendMailTo(trans, MailReceiver(owner), MailSender(leader));
	CharacterDatabase.CommitTransaction(trans);

	return true;
}


bool GuildTaskMgr::SendKillAdvertisement(uint32 creatureId, uint32 owner, uint32 guildId, uint32 validIn)
{
	Guild *guild = sGuildMgr->GetGuildById(guildId);
	Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());

	CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(creatureId);
	if (!proto)
		return false;

	QueryResult result = WorldDatabase.PQuery("SELECT map, position_x, position_y, position_z FROM creature where id = '%u'", creatureId);
	if (!result)
		return false;

	string location;
	do
	{
		Field* fields = result->Fetch();
		uint32 mapid = fields[0].GetUInt32();
		float x = fields[1].GetFloat();
		float y = fields[2].GetFloat();
		float z = fields[3].GetFloat();
		Map* map = sMapMgr->FindMap(mapid, 0);
		if (!map) continue;
		uint32 area = map->GetAreaId(x, y, z);
		const AreaTableEntry* entry = sAreaStore.LookupEntry(area);
		if (!entry) continue;
		location = entry->area_name[0];
		break;
	} while (result->NextRow());

	ostringstream body;
	body << GetHelloText(owner);
	body << "As you probably know " << proto->Name << " is wanted dead for the crimes it did against our guild. If you should kill it ";
	body << "we'd really appreciate that.\n\n";
	if (!location.empty())
		body << proto->Name << "'s the last known location was " << location << ".\n";
	body << "The task will expire at " << formatDateTime(validIn) << "\n";
	body << "\n";
	body << "Best Regards,\n";
	body << guild->GetName() << "\n";
	body << leader->GetName() << "\n";

	SQLTransaction trans = CharacterDatabase.BeginTransaction();
	ostringstream subject;
	subject << "Guild Task: ";
	if (proto->rank == CREATURE_ELITE_ELITE || proto->rank == CREATURE_ELITE_RAREELITE || proto->rank == CREATURE_ELITE_WORLDBOSS)
		subject << "(Elite) ";
	subject << proto->Name;
	if (!location.empty())
		subject << ", " << location;

	MailDraft(subject.str(), body.str()).SendMailTo(trans, MailReceiver(owner), MailSender(leader));
	CharacterDatabase.CommitTransaction(trans);

	return true;
}

bool GuildTaskMgr::SendThanks(uint32 owner, uint32 guildId)
{
	Guild *guild = sGuildMgr->GetGuildById(guildId);
	if (!guild)
		return false;

	Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
	if (!leader)
		return false;

	uint32 itemTask = GetTaskValue(owner, guildId, "itemTask");
	if (itemTask)
	{
		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemTask);
		if (!proto)
			return false;

		SQLTransaction trans = CharacterDatabase.BeginTransaction();
		ostringstream body;
		body << GetHelloText(owner);
		body << "One of our guild members wishes to thank you for the " << proto->Name1 << "!";
		uint32 count = GetTaskValue(owner, guildId, "itemCount");
		if (count)
		{
			body << " If we have another ";
			body << count << " of them that would help us tremendously.\n";
		}
		body << "\n";
		body << "Thanks again,\n";
		body << guild->GetName() << "\n";
		body << leader->GetName() << "\n";

		MailDraft("Thank You", body.str()).AddMoney(GetTaskValue(owner, guildId, "payment")).SendMailTo(trans, MailReceiver(owner), MailSender(leader));

		CharacterDatabase.CommitTransaction(trans);

		Player* player = ObjectAccessor::FindPlayer(owner);
		if (player)
			ChatHandler(player->GetSession()).PSendSysMessage("Guild task payment is pending");

		return true;
	}

	return false;
}

uint32 GuildTaskMgr::GetMaxItemTaskCount(uint32 itemId)
{
	ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
	if (!proto)
		return 0;

	if (proto->Quality == ITEM_QUALITY_NORMAL && proto->Stackable && proto->GetMaxStackSize() > 1)
		return urand(2, 4) * proto->GetMaxStackSize();
	else if (proto->Quality < ITEM_QUALITY_RARE && proto->Stackable && proto->GetMaxStackSize() > 1)
		return proto->GetMaxStackSize();
	else if (proto->Stackable && proto->GetMaxStackSize() > 1)
		return urand(1 + proto->GetMaxStackSize() / 4, proto->GetMaxStackSize());

	return 1;
}

bool GuildTaskMgr::IsGuildTaskItem(uint32 itemId, uint32 guildId)
{
	uint32 value = 0;

	QueryResult results = CharacterDatabase.PQuery(
		"select `value`, `time`, validIn from ai_playerbot_guild_tasks where `value` = '%u' and guildid = '%u' and `type` = 'itemTask'",
		itemId, guildId);

	if (results)
	{
		Field* fields = results->Fetch();
		value = fields[0].GetUInt32();
		uint32 lastChangeTime = fields[1].GetUInt32();
		uint32 validIn = fields[2].GetUInt32();
		if ((time(0) - lastChangeTime) >= validIn)
			value = 0;

	}

	return value;
}

map<uint32, uint32> GuildTaskMgr::GetTaskValues(uint32 owner, string type, uint32 *validIn /* = NULL */)
{
	map<uint32, uint32> result;

	QueryResult results = CharacterDatabase.PQuery(
		"select `value`, `time`, validIn, guildid from ai_playerbot_guild_tasks where owner = '%u' and `type` = '%s'",
		owner, type.c_str());

	if (!results)
		return result;

	do
	{
		Field* fields = results->Fetch();
		uint32 value = fields[0].GetUInt32();
		uint32 lastChangeTime = fields[1].GetUInt32();
		uint32 secs = fields[2].GetUInt32();
		uint32 guildId = fields[3].GetUInt32();
		if ((time(0) - lastChangeTime) >= secs)
			value = 0;

		result[guildId] = value;

	} while (results->NextRow());

	return result;
}

uint32 GuildTaskMgr::GetTaskValue(uint32 owner, uint32 guildId, string type, uint32 *validIn /* = NULL */)
{
	uint32 value = 0;

	QueryResult results = CharacterDatabase.PQuery(
		"select `value`, `time`, validIn from ai_playerbot_guild_tasks where owner = '%u' and guildid = '%u' and `type` = '%s'",
		owner, guildId, type.c_str());

	if (results)
	{
		Field* fields = results->Fetch();
		value = fields[0].GetUInt32();
		uint32 lastChangeTime = fields[1].GetUInt32();
		uint32 secs = fields[2].GetUInt32();
		if ((time(0) - lastChangeTime) >= secs)
			value = 0;

		if (validIn) *validIn = secs;
	}

	return value;
}

uint32 GuildTaskMgr::SetTaskValue(uint32 owner, uint32 guildId, string type, uint32 value, uint32 validIn)
{
	CharacterDatabase.DirectPExecute("delete from ai_playerbot_guild_tasks where owner = '%u' and guildid = '%u' and `type` = '%s'",
		owner, guildId, type.c_str());
	if (value)
	{
		CharacterDatabase.DirectPExecute(
			"insert into ai_playerbot_guild_tasks (owner, guildid, `time`, validIn, `type`, `value`) values ('%u', '%u', '%u', '%u', '%s', '%u')",
			owner, guildId, (uint32)time(0), validIn, type.c_str(), value);
	}

	return value;
}

bool GuildTaskMgr::HandleConsoleCommand(ChatHandler* handler, char const* args)
{
	if (!sPlayerbotAIConfig.guildTaskEnabled)
	{
		sLog->outError("Guild task system is currently disabled!");
		return false;
	}

	if (!args || !*args)
	{
		sLog->outError("Usage: gtask stats/reset");
		return false;
	}

	string cmd = args;

	if (cmd == "reset")
	{
		CharacterDatabase.PExecute("delete from ai_playerbot_guild_tasks");
		sLog->outString("Guild tasks were reset for all players");
		return true;
	}

	if (cmd == "stats")
	{
		sLog->outString("Usage: gtask stats <player name>");
		return true;
	}

	if (cmd.find("stats ") != string::npos)
	{
		string charName = cmd.substr(cmd.find("stats ") + 6);
		uint64 guid = sObjectMgr->GetPlayerGUIDByName(charName);
		if (!guid)
		{
			sLog->outError("Player %s not found", charName.c_str());
			return false;
		}

		uint32 owner = (uint32)guid;

		QueryResult result = CharacterDatabase.PQuery(
			"select `value`, `time`, validIn, guildid from ai_playerbot_guild_tasks where owner = '%u' and type='activeTask' order by guildid",
			owner);

		if (result)
		{
			do
			{
				Field* fields = result->Fetch();
				uint32 value = fields[0].GetUInt32();
				uint32 lastChangeTime = fields[1].GetUInt32();
				uint32 validIn = fields[2].GetUInt32();
				if ((time(0) - lastChangeTime) >= validIn)
					value = 0;
				uint32 guildId = fields[3].GetUInt32();

				Guild *guild = sGuildMgr->GetGuildById(guildId);
				if (!guild)
					continue;

				ostringstream name;
				if (value == GUILD_TASK_TYPE_ITEM)
				{
					name << "ItemTask";
					uint32 itemId = sGuildTaskMgr.GetTaskValue(owner, guildId, "itemTask");
					uint32 itemCount = sGuildTaskMgr.GetTaskValue(owner, guildId, "itemCount");
					ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
					if (proto)
					{
						name << " (" << proto->Name1 << " x" << itemCount << ",";
						switch (proto->Quality)
						{
						case ITEM_QUALITY_UNCOMMON:
							name << "green";
							break;
						case ITEM_QUALITY_NORMAL:
							name << "white";
							break;
						case ITEM_QUALITY_RARE:
							name << "blue";
							break;
						case ITEM_QUALITY_EPIC:
							name << "epic";
							break;
						}
						name << ")";
					}
				}
				else if (value == GUILD_TASK_TYPE_KILL)
				{
					name << "KillTask";
					uint32 creatureId = sGuildTaskMgr.GetTaskValue(owner, guildId, "killTask");
					CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(creatureId);
					if (proto)
					{
						name << " (" << proto->Name << ",";
						switch (proto->rank)
						{
						case CREATURE_ELITE_RARE:
							name << "rare";
							break;
						case CREATURE_ELITE_RAREELITE:
							name << "rare elite";
							break;
						}
						name << ")";
					}
				}
				else
					continue;

				uint32 advertValidIn = 0;
				uint32 advert = sGuildTaskMgr.GetTaskValue(owner, guildId, "advertisement", &advertValidIn);
				if (advert && advertValidIn < validIn)
					name << " advert in " << formatTime(advertValidIn);

				uint32 thanksValidIn = 0;
				uint32 thanks = sGuildTaskMgr.GetTaskValue(owner, guildId, "thanks", &thanksValidIn);
				if (thanks && thanksValidIn < validIn)
					name << " thanks in " << formatTime(thanksValidIn);

				uint32 rewardValidIn = 0;
				uint32 reward = sGuildTaskMgr.GetTaskValue(owner, guildId, "reward", &rewardValidIn);
				if (reward && rewardValidIn < validIn)
					name << " reward in " << formatTime(rewardValidIn);

				uint32 paymentValidIn = 0;
				uint32 payment = sGuildTaskMgr.GetTaskValue(owner, guildId, "payment", &paymentValidIn);
				if (payment && paymentValidIn < validIn)
					name << " payment " << ChatHelper::formatMoney(payment) << " in " << formatTime(paymentValidIn);

				sLog->outString("%s: %s valid in %s ['%s']",
					charName.c_str(), name.str().c_str(), formatTime(validIn).c_str(), guild->GetName().c_str());

			} while (result->NextRow());

			Field* fields = result->Fetch();
		}

		return true;
	}

	if (cmd == "reward")
	{
		sLog->outString("Usage: gtask reward <player name>");
		return true;
	}

	if (cmd == "advert")
	{
		sLog->outString("Usage: gtask advert <player name>");
		return true;
	}

	bool reward = cmd.find("reward ") != string::npos;
	bool advert = cmd.find("advert ") != string::npos;
	if (reward || advert)
	{
		string charName;
		if (reward) charName = cmd.substr(cmd.find("reward ") + 7);
		if (advert) charName = cmd.substr(cmd.find("advert ") + 7);
		uint64 guid = sObjectMgr->GetPlayerGUIDByName(charName);
		if (!guid)
		{
			sLog->outError("Player %s not found", charName.c_str());
			return false;
		}

		uint32 owner = (uint32)guid;
		QueryResult result = CharacterDatabase.PQuery(
			"select distinct guildid from ai_playerbot_guild_tasks where owner = '%u'",
			owner);

		if (result)
		{
			do
			{
				Field* fields = result->Fetch();
				uint32 guildId = fields[0].GetUInt32();
				Guild *guild = sGuildMgr->GetGuildById(guildId);
				if (!guild)
					continue;

				if (reward) sGuildTaskMgr.Reward(owner, guildId);
				if (advert) sGuildTaskMgr.SendAdvertisement(owner, guildId);
			} while (result->NextRow());

			Field* fields = result->Fetch();
			return true;
		}
	}

	return false;
}

void GuildTaskMgr::CheckItemTask(uint32 itemId, uint32 obtained, Player* ownerPlayer, Player* bot, bool byMail)
{
	uint32 guildId = bot->GetGuildId();
	if (!guildId)
		return;

	uint32 owner = (uint32)ownerPlayer->GetGUIDLow();
	Guild *guild = sGuildMgr->GetGuildById(bot->GetGuildId());
	if (!guild)
		return;

	sLog->outString("%s / %s: checking guild task",
		guild->GetName().c_str(), ownerPlayer->GetName().c_str());

	uint32 itemTask = GetTaskValue(owner, guildId, "itemTask");
	if (itemTask != itemId)
	{
		sLog->outString("%s / %s: item %u is not guild task item (%u)",
			guild->GetName().c_str(), ownerPlayer->GetName().c_str(),
			itemId, itemTask);
		return;
	}

	uint32 count = GetTaskValue(owner, guildId, "itemCount");
	if (!count) {
		return;
	}

	uint32 rewardTime = urand(sPlayerbotAIConfig.minGuildTaskRewardTime, sPlayerbotAIConfig.maxGuildTaskRewardTime);
	if (byMail)
	{
		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
		if (!proto)
			return;

		uint32 money = GetTaskValue(owner, guildId, "payment");
		SetTaskValue(owner, guildId, "payment", money + proto->BuyPrice * obtained, rewardTime - 45);
	}

	if (obtained >= count)
	{
		sLog->outString("%s / %s: guild task complete",
			guild->GetName().c_str(), ownerPlayer->GetName().c_str());
		SetTaskValue(owner, guildId, "reward", 1, rewardTime - 15);
		SetTaskValue(owner, guildId, "itemCount", 0, 0);
		SetTaskValue(owner, guildId, "thanks", 0, 0);
		ChatHandler(ownerPlayer->GetSession()).PSendSysMessage("You have completed a guild task");
	}
	else
	{
		sLog->outString("%s / %s: guild task progress %u/%u",
			guild->GetName().c_str(), ownerPlayer->GetName().c_str(), obtained, count);
		SetTaskValue(owner, guildId, "itemCount", count - obtained, sPlayerbotAIConfig.maxGuildTaskChangeTime);
		SetTaskValue(owner, guildId, "thanks", 1, rewardTime - 30);
		ChatHandler(ownerPlayer->GetSession()).PSendSysMessage("You have made a progress with a guild task");
	}
}

bool GuildTaskMgr::Reward(uint32 owner, uint32 guildId)
{
	Guild *guild = sGuildMgr->GetGuildById(guildId);
	if (!guild)
		return false;

	Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
	if (!leader)
		return false;

	uint32 itemTask = GetTaskValue(owner, guildId, "itemTask");
	uint32 killTask = GetTaskValue(owner, guildId, "killTask");
	if (!itemTask && !killTask)
		return false;

	ostringstream body;
	body << GetHelloText(owner);

	RandomItemType rewardType;
	if (itemTask)
	{
		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemTask);
		if (!proto)
			return false;

		body << "We wish to thank you for the " << proto->Name1 << " you provided so kindly. We really appreciate this and may this small gift bring you our thanks!\n";
		body << "\n";
		body << "Many thanks,\n";
		body << guild->GetName() << "\n";
		body << leader->GetName() << "\n";
		rewardType = proto->Quality > ITEM_QUALITY_NORMAL ? RANDOM_ITEM_GUILD_TASK_REWARD_EQUIP_BLUE : RANDOM_ITEM_GUILD_TASK_REWARD_EQUIP_GREEN;
	}
	else if (killTask)
	{
		CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(killTask);
		if (!proto)
			return false;

		body << "We wish to thank you for the " << proto->Name << " you've killed recently. We really appreciate this and may this small gift bring you our thanks!\n";
		body << "\n";
		body << "Many thanks,\n";
		body << guild->GetName() << "\n";
		body << leader->GetName() << "\n";
		rewardType = RANDOM_ITEM_GUILD_TASK_REWARD_TRADE;
	}

	SQLTransaction trans = CharacterDatabase.BeginTransaction();
	MailDraft draft("Thank You", body.str());

	uint32 itemId = sRandomItemMgr.GetRandomItem(rewardType);
	if (itemId)
	{
		Item* item = Item::CreateItem(itemId, 1, leader);
		item->SaveToDB(trans);
		draft.AddItem(item);
	}

	draft.AddMoney(GetTaskValue(owner, guildId, "payment")).SendMailTo(trans, MailReceiver(owner), MailSender(leader));
	CharacterDatabase.CommitTransaction(trans);

	Player* player = ObjectAccessor::FindPlayer(owner);
	if (player)
		ChatHandler(player->GetSession()).PSendSysMessage("Guild task reward is pending");

	SetTaskValue(owner, guildId, "activeTask", 0, 0);
	return true;
}

void GuildTaskMgr::CheckKillTask(Player* player, Unit* victim)
{
	Group *group = player->GetGroup();
	if (group)
	{
		for (GroupReference *gr = group->GetFirstMember(); gr; gr = gr->next())
		{
			CheckKillTaskInternal(gr->GetSource(), victim);
		}
	}
	else
	{
		CheckKillTaskInternal(player, victim);
	}
}

void GuildTaskMgr::CheckKillTaskInternal(Player* player, Unit* victim)
{
	uint32 owner = player->GetGUIDLow();
	Creature* creature = victim->ToCreature();
	if (!creature)
		return;

	map<uint32, uint32> tasks = GetTaskValues(owner, "killTask");
	for (map<uint32, uint32>::iterator i = tasks.begin(); i != tasks.end(); ++i)
	{
		uint32 guildId = i->first;
		uint32 value = i->second;
		Guild* guild = sGuildMgr->GetGuildById(guildId);

		if (value != creature->GetCreatureTemplate()->Entry)
			continue;

		sLog->outString("%s / %s: guild task complete",
			guild->GetName().c_str(), player->GetName().c_str());
		SetTaskValue(owner, guildId, "reward", 1,
			urand(sPlayerbotAIConfig.minGuildTaskRewardTime, sPlayerbotAIConfig.maxGuildTaskRewardTime));

		Group *group = player->GetGroup();
		if (group)
		{
			for (GroupReference *gr = group->GetFirstMember(); gr; gr = gr->next())
			{
				Player *member = gr->GetSource();
				if (member != player)
					ChatHandler(member->GetSession()).PSendSysMessage("%s has completed a guild task", player->GetName().c_str());
			}
		}
		ChatHandler(player->GetSession()).PSendSysMessage("You have completed a guild task");
	}
}