/**
 * =============================================================================
 * CleanerCS2
 * Copyright (C) 2024-2026 Poggu
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include "cleanercs2.h"
#include <iserver.h>
#include "khook.hpp"
#include "utils/module.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <mutex>
#include <shared_mutex>

// bruh
#undef POSIX
#include <re2/re2.h>

#ifdef _WIN32
#define ROOTBIN "/bin/win64/"
#define GAMEBIN "/csgo/bin/win64/"
#else
#define ROOTBIN "/bin/linuxsteamrt64/"
#define GAMEBIN "/csgo/bin/linuxsteamrt64/"
#endif

CleanerPlugin g_CleanerPlugin;
IServerGameDLL *server = NULL;
IServerGameClients *gameclients = NULL;
IVEngineServer *engine = NULL;
IGameEventManager2 *gameevents = NULL;
ICvar *icvar = NULL;

typedef int (*LogDirect_t)(void* loggingSystem, int channel, int severity, LeafCodeInfo_t*, char const*, va_list*);

std::vector<re2::RE2*> g_RegexList;
std::shared_mutex g_RegexMutex;
thread_local bool g_BypassFilter = false;

KHook::Return<int> Detour_LogDirect(void* loggingSystem, int channel, int severity, LeafCodeInfo_t* leafCode, char const* str, va_list* args)
{
	if (g_BypassFilter)
		return {KHook::Action::Ignore};

	char buffer[MAX_LOGGING_MESSAGE_LENGTH];

	if (args)
	{
		va_list args2;
		va_copy(args2, *args);
		V_vsnprintf(buffer, sizeof buffer, str, args2);
		va_end(args2);
	}

	{
		std::shared_lock<std::shared_mutex> lock(g_RegexMutex);
		for (auto& regex : g_RegexList)
		{
			if (RE2::FullMatch(args ? buffer : str, *regex))
				return {KHook::Action::Supersede, 0};
		}
	}

	return {KHook::Action::Ignore};
}

KHook::Function<int, void*, int, int, LeafCodeInfo_t*, char const*, va_list*> logDirectHook(Detour_LogDirect, nullptr);

bool SetupHook()
{
	CModule tier0Module(ROOTBIN, "tier0");

#ifdef WIN32
	const char* sig = "4C 89 4C 24 ? 44 89 44 24 ? 89 54 24 ? 55";
#else
	const char* sig = "55 89 D0 49 89 FA 89 F7 48 89 E5";
#endif
	LogDirect_t pLogDirect = reinterpret_cast<LogDirect_t>(KHook::LookupSignature(tier0Module.m_base, tier0Module.m_size, sig));

	if (!pLogDirect)
	{
		META_CONPRINTF("[CleanerCS2] Failed to find LogDirect signature\n");
		return false;
	}

	logDirectHook.Configure(pLogDirect);

	return true;
}

void LoadConfig()
{
	std::vector<re2::RE2*> fresh;

	CBufferStringGrowable<MAX_PATH> gameDir;
	engine->GetGameDir(gameDir);

	std::filesystem::path cfgPath = gameDir.Get();
	cfgPath /= "addons/cleanercs2/config.cfg";

	if (!std::filesystem::exists(cfgPath))
	{
		std::ofstream cfgFile(cfgPath);
		cfgFile.close();
	}

	std::ifstream cfgFile(cfgPath);

	if (cfgFile.is_open())
	{
		std::string line;
		while (std::getline(cfgFile, line))
		{
			// allow CRLF on linux
			line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

			if (line.empty())
				continue;

			if (line[0] == '/' && line[1] == '/')
				continue;

			g_BypassFilter = true;
			META_CONPRINTF("Registering regex: %s\n", line.c_str());
			g_BypassFilter = false;

			RE2::Options options;
			options.set_dot_nl(true);

			RE2* re = new RE2(line, options);

			if (re->ok())
				fresh.push_back(re);
			else
				META_CONPRINTF("[CleanerCS2] Failed to parse regex: '%s': %s\n", line.c_str(), re->error().c_str());
		}
		cfgFile.close();
	}
	else
	{
		META_CONPRINTF("[CleanerCS2] Failed to open config file\n");
	}

	std::vector<re2::RE2*> old;
	{
		std::unique_lock<std::shared_mutex> lock(g_RegexMutex);
		old.swap(g_RegexList);
		g_RegexList.swap(fresh);
	}

	for (auto& regex : old)
		delete regex;
}

CON_COMMAND_F(conclear_reload, "Reloads the cleaner config", FCVAR_SPONLY | FCVAR_LINKED_CONCOMMAND)
{
	LoadConfig();
}

PLUGIN_EXPOSE(CleanerPlugin, g_CleanerPlugin);
bool CleanerPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	g_pCVar = icvar;
	META_CONVAR_REGISTER( FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL );

	LoadConfig();

	if (!SetupHook())
	{
		META_CONPRINTF("[CleanerCS2] Failed to setup hook\n");
		return false;
	}

	return true;
}

bool CleanerPlugin::Unload(char *error, size_t maxlen)
{
	std::unique_lock<std::shared_mutex> lock(g_RegexMutex);

	for (auto& regex : g_RegexList)
		delete regex;

	g_RegexList.clear();

	return true;
}

void CleanerPlugin::AllPluginsLoaded()
{
}

void CleanerPlugin::OnLevelInit( char const *pMapName,
									 char const *pMapEntities,
									 char const *pOldLevel,
									 char const *pLandmarkName,
									 bool loadGame,
									 bool background )
{
	META_CONPRINTF("OnLevelInit(%s)\n", pMapName);
}

void CleanerPlugin::OnLevelShutdown()
{
	META_CONPRINTF("OnLevelShutdown()\n");
}

bool CleanerPlugin::Pause(char *error, size_t maxlen)
{
	return true;
}

bool CleanerPlugin::Unpause(char *error, size_t maxlen)
{
	return true;
}

const char *CleanerPlugin::GetLicense()
{
	return "GPLv3";
}

const char *CleanerPlugin::GetVersion()
{
	return "2.0";
}

const char *CleanerPlugin::GetDate()
{
	return __DATE__;
}

const char *CleanerPlugin::GetLogTag()
{
	return "CLEANER";
}

const char *CleanerPlugin::GetAuthor()
{
	return "Poggu";
}

const char *CleanerPlugin::GetDescription()
{
	return "Console regex filter";
}

const char *CleanerPlugin::GetName()
{
	return "CleanerCS2";
}

const char *CleanerPlugin::GetURL()
{
	return "https://poggu.me";
}
