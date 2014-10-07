// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APPS_APP_INFO_DIALOG_H_
#define CHROME_BROWSER_UI_APPS_APP_INFO_DIALOG_H_

#include "base/callback_forward.h"
#include "ui/gfx/native_widget_types.h"

class Profile;

namespace extensions {
class Extension;
}

namespace gfx {
class Rect;
class Size;
}

// Used for UMA to track where the App Info dialog is launched from.
enum AppInfoLaunchSource {
  FROM_APP_LIST,         // Launched from the app list context menu.
  FROM_EXTENSIONS_PAGE,  // Launched from the chrome://extensions page.
  NUM_LAUNCH_SOURCES,
};

// Shows the chrome app information as a frameless window for the given |app|
// and |profile| at the given |app_list_bounds|. Appears 'inside' the app list.
void ShowAppInfoInAppList(gfx::NativeWindow parent,
                          const gfx::Rect& app_list_bounds,
                          Profile* profile,
                          const extensions::Extension* app,
                          const base::Closure& close_callback);

// Shows the chrome app information in a native dialog box of the given |size|.
void ShowAppInfoInNativeDialog(gfx::NativeWindow parent,
                               const gfx::Size& size,
                               Profile* profile,
                               const extensions::Extension* app,
                               const base::Closure& close_callback);

#endif  // CHROME_BROWSER_UI_APPS_APP_INFO_DIALOG_H_
