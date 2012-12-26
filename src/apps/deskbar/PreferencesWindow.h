/*
 * Copyright 2009 Haiku, Inc.
 * All Rights Reserved. Distributed under the terms of the MIT License.
 */
#ifndef _PREFERENCES_WINDOW_H
#define _PREFERENCES_WINDOW_H


#include <Window.h>


const uint32 kConfigShow			= 'show';
const uint32 kUpdateRecentCounts	= 'upct';
const uint32 kEditMenuInTracker		= 'mtrk';

const uint32 kTrackerFirst			= 'TkFt';
const uint32 kSortRunningApps		= 'SAps';
const uint32 kSuperExpando			= 'SprE';
const uint32 kExpandNewTeams		= 'ExTm';
const uint32 kHideLabels			= 'hLbs';
const uint32 kResizeTeamIcons		= 'RTIs';
const uint32 kAutoRaise				= 'AtRs';
const uint32 kAutoHide				= 'AtHd';

class BBox;
class BButton;
class BCheckBox;
class BListView;
class BRadioButton;
class BSlider;
class BStringView;
class BTextControl;

class PreferencesWindow : public BWindow
{
public:
							PreferencesWindow(BRect frame);
							~PreferencesWindow();

		virtual	void		MessageReceived(BMessage* message);
		virtual	bool		QuitRequested();
		virtual	void		WindowActivated(bool active);

				void		UpdateRecentCounts();
				void		EnableDisableDependentItems();

private:
				void		_HandleChangedSettingsView();

			BListView*		fSettingsTypeListView;
			BBox*			fSettingsContainerBox;

			BCheckBox*		fMenuRecentDocuments;
			BCheckBox*		fMenuRecentApplications;
			BCheckBox*		fMenuRecentFolders;

			BTextControl*	fMenuRecentDocumentCount;
			BTextControl*	fMenuRecentApplicationCount;
			BTextControl*	fMenuRecentFolderCount;

			BCheckBox*		fAppsSort;
			BCheckBox*		fAppsSortTrackerFirst;
			BCheckBox*		fAppsShowExpanders;
			BCheckBox*		fAppsExpandNew;
			BCheckBox*		fAppsHideLabels;
			BSlider*		fAppsIconSizeSlider;

			BCheckBox*		fWindowAlwaysOnTop;
			BCheckBox*		fWindowAutoRaise;
			BCheckBox*		fWindowAutoHide;
};


#endif	/* _PREFERENCES_WINDOW_H */
