// Copyright (c) 2024 The Source Authors.  All rights reserved.
//
// POSIX/No-Steam stand-in for the Steamworks SDK header
// "achievements_and_stats_interface.h".
//
// Upstream, that header declares Valve's Steam stats-upload interface
// (class AchievementsAndStatsInterface), which the CS:S-specific
// CSAchievementsAndStatsInterface (game/shared/cs_achievements_and_stats_interface.h)
// derives from. The SDK header is not in this tree, and with NO_STEAM=1 there
// is no backend to upload to -- but the achievements *UI* (panel, summary
// pages, trackers) is pure VGUI and works offline, so the two CS TUs
// (cs_achievements_and_stats_interface.cpp, cstrike/achievements_cs.cpp)
// stay in the client build instead of being excluded.
//
// This header lives under cmake/compat/, which is already on the include path
// of every target, so no per-target include needs to be added. It declares
// exactly what the in-tree derived class uses: a base with a default ctor,
// the three panel virtuals plus GetAchievementsPanelMinWidth, and the global
// the implementation publishes (referenced nowhere else in the tree).

#ifndef SE_COMPAT_ACHIEVEMENTS_AND_STATS_INTERFACE_H_
#define SE_COMPAT_ACHIEVEMENTS_AND_STATS_INTERFACE_H_

#ifdef _WIN32
#pragma once
#endif

namespace vgui
{
class Panel;
}

class AchievementsAndStatsInterface
{
public:
	AchievementsAndStatsInterface() {}
	virtual ~AchievementsAndStatsInterface() {}

	virtual void CreatePanel( vgui::Panel *pParent ) { (void)pParent; }
	virtual void DisplayPanel() {}
	virtual void ReleasePanel() {}
	virtual int GetAchievementsPanelMinWidth() const { return 0; }
};

extern AchievementsAndStatsInterface *g_pAchievementsAndStatsInterface;

#endif  // !SE_COMPAT_ACHIEVEMENTS_AND_STATS_INTERFACE_H_
