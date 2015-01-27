// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/component_updater/pref_names.h"

namespace prefs {

// String that represents the recovery component last downloaded version. This
// takes the usual 'a.b.c.d' notation.
const char kRecoveryComponentVersion[] = "recovery_component.version";

// Full path where last recovery component CRX was unpacked to.
const char kRecoveryComponentUnpackPath[] = "recovery_component.unpack_path";

#if defined(OS_WIN)
// The last exit code integer value returned by the SwReporter. Saved in local
// state.
const char kSwReporterLastExitCode[] = "software_reporter.last_exit_code";

// The last time SwReporter was triggered. Saved in local state.
const char kSwReporterLastTimeTriggered[] =
    "software_reporter.last_time_triggered";

// The exit code integer value of the reporter run that triggered an SRT prompt.
// Stored in the protected prefs of the profile that owns the browser where the
// prompt was shown.
const char kSwReporterPromptReason[] = "software_reporter.prompt_reason";

// The version string of the reporter that triggered an SRT prompt. An empty
// string when the prompt wasn't shown yet. Stored in the protected prefs of the
// profile that owns the browser where the prompt was shown.
const char kSwReporterPromptVersion[] = "software_reporter.prompt_version";

// A string value uniquely identifying an SRTPrompt campaign so that users that
// have been prompted with this seed before won't be prompted again until a new
// seed comes in.
const char kSwReporterPromptSeed[] = "software_reporter.prompt_seed";
#endif

}  // namespace prefs
