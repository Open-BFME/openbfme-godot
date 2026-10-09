// OpenBFME. GPL-3.0.
//
// The screen factory table of the Shell (spec menus-apt.md 3.3, step A4): the screen files the engine can create.
// Donor facts: BFME1 AptScreenFactories.cpp pairs fifteen screens with their constructors (LoadScreen, LanLobby, MainMenu,
// OnlineShell, Options, DisconnectScreen, SaveLoad, Skirmish, QuitMenu, Objectives/PlayerStatus, ScoreScreen, CampaignReview,
// InGameChat, SpellStore); this lane's A4 table is the subset the brief names plus the three window-manager-loaded movies
// (AptSimpleScreens.h).  Later lanes add the rest through registerFactory().

#pragma once

#include "GameClient/GUI/Shell/Shell.h"

void registerAptScreenFactories(AptScreenFactoryTable &table);
