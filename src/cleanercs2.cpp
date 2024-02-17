/**
 * =============================================================================
 * CleanerCS2
 * Copyright (C) 2024 Poggu
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
#include <funchook.h>
#include "utils/module.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

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

typedef int (*LogDirect_t)(void* loggingSystem, int channel, int severity, LeafCodeInfo_t*, LoggingMetaData_t*, Color, char const*, va_list*);
LogDirect_t g_pLogDirect = nullptr;
funchook_t* g_pHook = nullptr;

std::vector<re2::RE2*> g_RegexList;

int Detour_LogDirect(void* loggingSystem, int channel, int severity, LeafCodeInfo_t* leafCode, LoggingMetaData_t* metaData, Color color, char const* str, va_list* args)
{
	char buffer[MAX_LOGGING_MESSAGE_LENGTH];

	if (args)
	{
		va_list args2;
		va_copy(args2, *args);
		V_vsnprintf(buffer, sizeof buffer, str, args2);
		va_end(args2);
	}

	for (auto& regex : g_RegexList)
	{
		if (RE2::FullMatch(args ? buffer : str, *regex))
			return 0;
	}

	return g_pLogDirect(loggingSystem, channel, severity, leafCode, metaData, color, str, args);
}

bool SetupHook()
{
	auto serverModule = new CModule(ROOTBIN, "tier0");

	int err;
#ifdef WIN32
	const byte sig[] = "\x4C\x89\x4C\x24\x20\x44\x89\x44\x24\x18\x89\x54\x24\x10\x55";
#else
	const byte sig[] = "\x55\x89\xD0\x48\x89\xE5\x41\x57\x41\x56\x41\x55";
#endif
	g_pLogDirect = (LogDirect_t)serverModule->FindSignature((byte*)sig, sizeof(sig) - 1, err);

	if (err)
	{
		META_CONPRINTF("[CleanerCS2] Failed to find signature: %i\n", err);
		return false;
	}

	auto g_pHook = funchook_create();
	funchook_prepare(g_pHook, (void**)&g_pLogDirect, (void*)Detour_LogDirect);
	funchook_install(g_pHook, 0);

	return true;
}

void LoadConfig()
{
	for(auto& regex : g_RegexList)
		delete regex;

	g_RegexList.clear();

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
			if (line[0] == '/' && line[1] == '/')
				continue;

			if (line.empty())
				continue;

			// allow CRLF on linux
			line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

			META_CONPRINTF("Registering regex: %s\n", line.c_str());

			RE2::Options options;
			options.set_dot_nl(true);

			RE2* re = new RE2(line, options);

			if (re->ok())
				g_RegexList.push_back(re);
			else
				META_CONPRINTF("[CleanerCS2] Failed to parse regex: '%s': %s\n", line.c_str(), re->error().c_str());
		}
		cfgFile.close();
	}
	else
	{
		META_CONPRINTF("[CleanerCS2] Failed to open config file\n");
	}
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
	ConVar_Register( FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL );

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
	for (auto& regex : g_RegexList)
		delete regex;

	g_RegexList.clear();

	if (g_pHook)
	{
		funchook_uninstall(g_pHook, 0);
		funchook_destroy(g_pHook);
		g_pHook = nullptr;
	}

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
	return "1.0.0";
}

const char *CleanerPlugin::GetDate()
{
	return __DATE__;
}

const char *CleanerPlugin::GetLogTag()
{
	return "SAMPLE";
}

const char *CleanerPlugin::GetAuthor()
{
	return "AlliedModders LLC";
}

const char *CleanerPlugin::GetDescription()
{
	return "Sample basic plugin";
}

const char *CleanerPlugin::GetName()
{
	return "CleanerCS2";
}

const char *CleanerPlugin::GetURL()
{
	return "https://poggu.me";
}
