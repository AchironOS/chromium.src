// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A library to manage RLZ information for access-points shared
// across different client applications.

#include "rlz/lib/rlz_lib.h"

#include "base/string_util.h"
#include "base/stringprintf.h"
#include "rlz/lib/assert.h"
#include "rlz/lib/crc32.h"
#include "rlz/lib/financial_ping.h"
#include "rlz/lib/lib_values.h"
#include "rlz/lib/rlz_value_store.h"
#include "rlz/lib/string_utils.h"

#if defined(OS_CHROMEOS)
#include "rlz/chromeos/lib/rlz_value_store_chromeos.h"
#endif  // defined(OS_CHROMEOS)

namespace {

// Event information returned from ping response.
struct ReturnedEvent {
  rlz_lib::AccessPoint access_point;
  rlz_lib::Event event_type;
};

// Helper functions

bool IsAccessPointSupported(rlz_lib::AccessPoint point) {
  switch (point) {
  case rlz_lib::NO_ACCESS_POINT:
  case rlz_lib::LAST_ACCESS_POINT:

  case rlz_lib::MOBILE_IDLE_SCREEN_BLACKBERRY:
  case rlz_lib::MOBILE_IDLE_SCREEN_WINMOB:
  case rlz_lib::MOBILE_IDLE_SCREEN_SYMBIAN:
    // These AP's are never available on Windows PCs.
    return false;

  case rlz_lib::IE_DEFAULT_SEARCH:
  case rlz_lib::IE_HOME_PAGE:
  case rlz_lib::IETB_SEARCH_BOX:
  case rlz_lib::QUICK_SEARCH_BOX:
  case rlz_lib::GD_DESKBAND:
  case rlz_lib::GD_SEARCH_GADGET:
  case rlz_lib::GD_WEB_SERVER:
  case rlz_lib::GD_OUTLOOK:
  case rlz_lib::CHROME_OMNIBOX:
  case rlz_lib::CHROME_HOME_PAGE:
    // TODO: Figure out when these settings are set to Google.

  default:
    return true;
  }
}

// Current RLZ can only use [a-zA-Z0-9_\-]
// We will be more liberal and allow some additional chars, but not url meta
// chars.
bool IsGoodRlzChar(const char ch) {
  if (IsAsciiAlpha(ch) || IsAsciiDigit(ch))
    return true;

  switch (ch) {
    case '_':
    case '-':
    case '!':
    case '@':
    case '$':
    case '*':
    case '(':
    case ')':
    case ';':
    case '.':
    case '<':
    case '>':
    return true;
  }

  return false;
}

// This function will remove bad rlz chars and also limit the max rlz to some
// reasonable size.  It also assumes that normalized_rlz is at least
// kMaxRlzLength+1 long.
void NormalizeRlz(const char* raw_rlz, char* normalized_rlz) {
  size_t index = 0;
  for (; raw_rlz[index] != 0 && index < rlz_lib::kMaxRlzLength; ++index) {
    char current = raw_rlz[index];
    if (IsGoodRlzChar(current)) {
      normalized_rlz[index] = current;
    } else {
      normalized_rlz[index] = '.';
    }
  }

  normalized_rlz[index] = 0;
}

void GetEventsFromResponseString(
    const std::string& response_line,
    const std::string& field_header,
    std::vector<ReturnedEvent>* event_array) {
  // Get the string of events.
  std::string events = response_line.substr(field_header.size());
  TrimWhitespaceASCII(events, TRIM_LEADING, &events);

  int events_length = events.find_first_of("\r\n ");
  if (events_length < 0)
    events_length = events.size();
  events = events.substr(0, events_length);

  // Break this up into individual events
  int event_end_index = -1;
  do {
    int event_begin = event_end_index + 1;
    event_end_index = events.find(rlz_lib::kEventsCgiSeparator, event_begin);
    int event_end = event_end_index;
    if (event_end < 0)
      event_end = events_length;

    std::string event_string = events.substr(event_begin,
                                             event_end - event_begin);
    if (event_string.size() != 3)  // 3 = 2(AP) + 1(E)
      continue;

    rlz_lib::AccessPoint point = rlz_lib::NO_ACCESS_POINT;
    rlz_lib::Event event = rlz_lib::INVALID_EVENT;
    if (!GetAccessPointFromName(event_string.substr(0, 2).c_str(), &point) ||
        point == rlz_lib::NO_ACCESS_POINT) {
      continue;
    }

    if (!GetEventFromName(event_string.substr(event_string.size() - 1).c_str(),
                          &event) || event == rlz_lib::INVALID_EVENT) {
      continue;
    }

    ReturnedEvent current_event = {point, event};
    event_array->push_back(current_event);
  } while (event_end_index >= 0);
}

// Event storage functions.
bool RecordStatefulEvent(rlz_lib::Product product, rlz_lib::AccessPoint point,
                         rlz_lib::Event event) {
  rlz_lib::ScopedRlzValueStoreLock lock;
  rlz_lib::RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(rlz_lib::RlzValueStore::kWriteAccess))
    return false;

  // Write the new event to the value store.
  const char* point_name = GetAccessPointName(point);
  const char* event_name = GetEventName(event);
  if (!point_name || !event_name)
    return false;

  if (!point_name[0] || !event_name[0])
    return false;

  std::string new_event_value;
  base::StringAppendF(&new_event_value, "%s%s", point_name, event_name);
  return store->AddStatefulEvent(product, new_event_value.c_str());
}

bool GetProductEventsAsCgiHelper(rlz_lib::Product product, char* cgi,
                                 size_t cgi_size,
                                 rlz_lib::RlzValueStore* store) {
  // Prepend the CGI param key to the buffer.
  std::string cgi_arg;
  base::StringAppendF(&cgi_arg, "%s=", rlz_lib::kEventsCgiVariable);
  if (cgi_size <= cgi_arg.size())
    return false;

  size_t index;
  for (index = 0; index < cgi_arg.size(); ++index)
    cgi[index] = cgi_arg[index];

  // Read stored events.
  std::vector<std::string> events;
  if (!store->ReadProductEvents(product, &events))
    return false;

  // Append the events to the buffer.
  size_t num_values = 0;

  for (num_values = 0; num_values < events.size(); ++num_values) {
    cgi[index] = '\0';

    int divider = num_values > 0 ? 1 : 0;
    int size = cgi_size - (index + divider);
    if (size <= 0)
      return cgi_size >= (rlz_lib::kMaxCgiLength + 1);

    strncpy(cgi + index + divider, events[num_values].c_str(), size);
    if (divider)
      cgi[index] = rlz_lib::kEventsCgiSeparator;

    index += std::min((int)events[num_values].length(), size) + divider;
  }

  cgi[index] = '\0';

  return num_values > 0;
}

}  // namespace

namespace rlz_lib {

#if defined(RLZ_NETWORK_IMPLEMENTATION_CHROME_NET)
bool SetURLRequestContext(net::URLRequestContextGetter* context) {
  return FinancialPing::SetURLRequestContext(context);
}
#endif

#if defined(OS_CHROMEOS)
void RLZ_LIB_API SetIOTaskRunner(base::SequencedTaskRunner* io_task_runner) {
  RlzValueStoreChromeOS::SetIOTaskRunner(io_task_runner);
}

void RLZ_LIB_API CleanupRlz() {
  RlzValueStoreChromeOS::Cleanup();
}
#endif

bool GetProductEventsAsCgi(Product product, char* cgi, size_t cgi_size) {
  if (!cgi || cgi_size <= 0) {
    ASSERT_STRING("GetProductEventsAsCgi: Invalid buffer");
    return false;
  }

  cgi[0] = 0;

  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kReadAccess))
    return false;

  size_t size_local = std::min(
      static_cast<size_t>(kMaxCgiLength + 1), cgi_size);
  bool result = GetProductEventsAsCgiHelper(product, cgi, size_local, store);

  if (!result) {
    ASSERT_STRING("GetProductEventsAsCgi: Possibly insufficient buffer size");
    cgi[0] = 0;
    return false;
  }

  return true;
}

bool RecordProductEvent(Product product, AccessPoint point, Event event) {
  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kWriteAccess))
    return false;

  // Get this event's value.
  const char* point_name = GetAccessPointName(point);
  const char* event_name = GetEventName(event);
  if (!point_name || !event_name)
    return false;

  if (!point_name[0] || !event_name[0])
    return false;

  std::string new_event_value;
  base::StringAppendF(&new_event_value, "%s%s", point_name, event_name);

  // Check whether this event is a stateful event. If so, don't record it.
  if (store->IsStatefulEvent(product, new_event_value.c_str())) {
    // For a stateful event we skip recording, this function is also
    // considered successful.
    return true;
  }

  // Write the new event to the value store.
  return store->AddProductEvent(product, new_event_value.c_str());
}

bool ClearProductEvent(Product product, AccessPoint point, Event event) {
  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kWriteAccess))
    return false;

  // Get the event's value store value and delete it.
  const char* point_name = GetAccessPointName(point);
  const char* event_name = GetEventName(event);
  if (!point_name || !event_name)
    return false;

  if (!point_name[0] || !event_name[0])
    return false;

  std::string event_value;
  base::StringAppendF(&event_value, "%s%s", point_name, event_name);
  return store->ClearProductEvent(product, event_value.c_str());
}

// RLZ storage functions.

bool GetAccessPointRlz(AccessPoint point, char* rlz, size_t rlz_size) {
  if (!rlz || rlz_size <= 0) {
    ASSERT_STRING("GetAccessPointRlz: Invalid buffer");
    return false;
  }

  rlz[0] = 0;

  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kReadAccess))
    return false;

  if (!IsAccessPointSupported(point))
    return false;

  return store->ReadAccessPointRlz(point, rlz, rlz_size);
}

bool SetAccessPointRlz(AccessPoint point, const char* new_rlz) {
  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kWriteAccess))
    return false;

  if (!new_rlz) {
    ASSERT_STRING("SetAccessPointRlz: Invalid buffer");
    return false;
  }

  // Return false if the access point is not set to Google.
  if (!IsAccessPointSupported(point)) {
    ASSERT_STRING(("SetAccessPointRlz: "
                "Cannot set RLZ for unsupported access point."));
    return false;
  }

  // Verify the RLZ length.
  size_t rlz_length = strlen(new_rlz);
  if (rlz_length > kMaxRlzLength) {
    ASSERT_STRING("SetAccessPointRlz: RLZ length is exceeds max allowed.");
    return false;
  }

  char normalized_rlz[kMaxRlzLength + 1];
  NormalizeRlz(new_rlz, normalized_rlz);
  VERIFY(strlen(new_rlz) == rlz_length);

  // Setting RLZ to empty == clearing.
  if (normalized_rlz[0] == 0)
    return store->ClearAccessPointRlz(point);
  return store->WriteAccessPointRlz(point, normalized_rlz);
}

// Financial Server pinging functions.

bool FormFinancialPingRequest(Product product, const AccessPoint* access_points,
                              const char* product_signature,
                              const char* product_brand,
                              const char* product_id,
                              const char* product_lang,
                              bool exclude_machine_id,
                              char* request, size_t request_buffer_size) {
  if (!request || request_buffer_size == 0)
    return false;

  request[0] = 0;

  std::string request_string;
  if (!FinancialPing::FormRequest(product, access_points, product_signature,
                                  product_brand, product_id, product_lang,
                                  exclude_machine_id, &request_string))
    return false;

  if (request_string.size() >= request_buffer_size)
    return false;

  strncpy(request, request_string.c_str(), request_buffer_size);
  request[request_buffer_size - 1] = 0;
  return true;
}

bool PingFinancialServer(Product product, const char* request, char* response,
                         size_t response_buffer_size) {
  if (!response || response_buffer_size == 0)
    return false;
  response[0] = 0;

  // Check if the time is right to ping.
  if (!FinancialPing::IsPingTime(product, false))
    return false;

  // Send out the ping.
  std::string response_string;
  if (!FinancialPing::PingServer(request, &response_string))
    return false;

  if (response_string.size() >= response_buffer_size)
    return false;

  strncpy(response, response_string.c_str(), response_buffer_size);
  response[response_buffer_size - 1] = 0;
  return true;
}

bool IsPingResponseValid(const char* response, int* checksum_idx) {
  if (!response || !response[0])
    return false;

  if (checksum_idx)
    *checksum_idx = -1;

  if (strlen(response) > kMaxPingResponseLength) {
    ASSERT_STRING("IsPingResponseValid: response is too long to parse.");
    return false;
  }

  // Find the checksum line.
  std::string response_string(response);

  std::string checksum_param("\ncrc32: ");
  int calculated_crc;
  int checksum_index = response_string.find(checksum_param);
  if (checksum_index >= 0) {
    // Calculate checksum of message preceeding checksum line.
    // (+ 1 to include the \n)
    std::string message(response_string.substr(0, checksum_index + 1));
    if (!Crc32(message.c_str(), &calculated_crc))
      return false;
  } else {
    checksum_param = "crc32: ";  // Empty response case.
    if (!StartsWithASCII(response_string, checksum_param, true))
      return false;

    checksum_index = 0;
    if (!Crc32("", &calculated_crc))
      return false;
  }

  // Find the checksum value on the response.
  int checksum_end = response_string.find("\n", checksum_index + 1);
  if (checksum_end < 0)
    checksum_end = response_string.size();

  int checksum_begin = checksum_index + checksum_param.size();
  std::string checksum = response_string.substr(checksum_begin,
      checksum_end - checksum_begin + 1);
  TrimWhitespaceASCII(checksum, TRIM_ALL, &checksum);

  if (checksum_idx)
    *checksum_idx = checksum_index;

  return calculated_crc == HexStringToInteger(checksum.c_str());
}

// Complex helpers built on top of other functions.

bool ParseFinancialPingResponse(Product product, const char* response) {
  // Update the last ping time irrespective of success.
  FinancialPing::UpdateLastPingTime(product);
  // Parse the ping response - update RLZs, clear events.
  return ParsePingResponse(product, response);
}

bool SendFinancialPing(Product product, const AccessPoint* access_points,
                       const char* product_signature,
                       const char* product_brand,
                       const char* product_id, const char* product_lang,
                       bool exclude_machine_id) {
  return SendFinancialPing(product, access_points, product_signature,
                           product_brand, product_id, product_lang,
                           exclude_machine_id, false);
}


bool SendFinancialPing(Product product, const AccessPoint* access_points,
                       const char* product_signature,
                       const char* product_brand,
                       const char* product_id, const char* product_lang,
                       bool exclude_machine_id,
                       const bool skip_time_check) {
  // Create the financial ping request.
  std::string request;
  if (!FinancialPing::FormRequest(product, access_points, product_signature,
                                  product_brand, product_id, product_lang,
                                  exclude_machine_id, &request))
    return false;

  // Check if the time is right to ping.
  if (!FinancialPing::IsPingTime(product, skip_time_check))
    return false;

  // Send out the ping, update the last ping time irrespective of success.
  FinancialPing::UpdateLastPingTime(product);
  std::string response;
  if (!FinancialPing::PingServer(request.c_str(), &response))
    return false;

  // Parse the ping response - update RLZs, clear events.
  return ParsePingResponse(product, response.c_str());
}

// TODO: Use something like RSA to make sure the response is
// from a Google server.
bool ParsePingResponse(Product product, const char* response) {
  rlz_lib::ScopedRlzValueStoreLock lock;
  rlz_lib::RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(rlz_lib::RlzValueStore::kWriteAccess))
    return false;

  std::string response_string(response);
  int response_length = -1;
  if (!IsPingResponseValid(response, &response_length))
    return false;

  if (0 == response_length)
    return true;  // Empty response - no parsing.

  std::string events_variable;
  std::string stateful_events_variable;
  base::SStringPrintf(&events_variable, "%s: ", kEventsCgiVariable);
  base::SStringPrintf(&stateful_events_variable, "%s: ",
                      kStatefulEventsCgiVariable);

  int rlz_cgi_length = strlen(kRlzCgiVariable);

  // Split response lines. Expected response format is lines of the form:
  // rlzW1: 1R1_____en__252
  int line_end_index = -1;
  do {
    int line_begin = line_end_index + 1;
    line_end_index = response_string.find("\n", line_begin);

    int line_end = line_end_index;
    if (line_end < 0)
      line_end = response_length;

    if (line_end <= line_begin)
      continue;  // Empty line.

    std::string response_line;
    response_line = response_string.substr(line_begin, line_end - line_begin);

    if (StartsWithASCII(response_line, kRlzCgiVariable, true)) {  // An RLZ.
      int separator_index = -1;
      if ((separator_index = response_line.find(": ")) < 0)
        continue;  // Not a valid key-value pair.

      // Get the access point.
      std::string point_name =
        response_line.substr(3, separator_index - rlz_cgi_length);
      AccessPoint point = NO_ACCESS_POINT;
      if (!GetAccessPointFromName(point_name.c_str(), &point) ||
          point == NO_ACCESS_POINT)
        continue;  // Not a valid access point.

      // Get the new RLZ.
      std::string rlz_value(response_line.substr(separator_index + 2));
      TrimWhitespaceASCII(rlz_value, TRIM_LEADING, &rlz_value);

      size_t rlz_length = rlz_value.find_first_of("\r\n ");
      if (rlz_length == std::string::npos)
        rlz_length = rlz_value.size();

      if (rlz_length > kMaxRlzLength)
        continue;  // Too long.

      if (IsAccessPointSupported(point))
        SetAccessPointRlz(point, rlz_value.substr(0, rlz_length).c_str());
    } else if (StartsWithASCII(response_line, events_variable, true)) {
      // Clear events which server parsed.
      std::vector<ReturnedEvent> event_array;
      GetEventsFromResponseString(response_line, events_variable, &event_array);
      for (size_t i = 0; i < event_array.size(); ++i) {
        ClearProductEvent(product, event_array[i].access_point,
                          event_array[i].event_type);
      }
    } else if (StartsWithASCII(response_line, stateful_events_variable, true)) {
      // Record any stateful events the server send over.
      std::vector<ReturnedEvent> event_array;
      GetEventsFromResponseString(response_line, stateful_events_variable,
                                  &event_array);
      for (size_t i = 0; i < event_array.size(); ++i) {
        RecordStatefulEvent(product, event_array[i].access_point,
                            event_array[i].event_type);
      }
    }
  } while (line_end_index >= 0);

#if defined(OS_WIN)
  // Update the DCC in registry if needed.
  SetMachineDealCodeFromPingResponse(response);
#endif

  return true;
}

bool GetPingParams(Product product, const AccessPoint* access_points,
                   char* cgi, size_t cgi_size) {
  if (!cgi || cgi_size <= 0) {
    ASSERT_STRING("GetPingParams: Invalid buffer");
    return false;
  }

  cgi[0] = 0;

  if (!access_points) {
    ASSERT_STRING("GetPingParams: access_points is NULL");
    return false;
  }

  // Add the RLZ Exchange Protocol version.
  std::string cgi_string(kProtocolCgiArgument);

  // Copy the &rlz= over.
  base::StringAppendF(&cgi_string, "&%s=", kRlzCgiVariable);

  {
    // Now add each of the RLZ's. Keep the lock during all GetAccessPointRlz()
    // calls below.
    ScopedRlzValueStoreLock lock;
    RlzValueStore* store = lock.GetStore();
    if (!store || !store->HasAccess(RlzValueStore::kReadAccess))
      return false;
    bool first_rlz = true;  // comma before every RLZ but the first.
    for (int i = 0; access_points[i] != NO_ACCESS_POINT; i++) {
      char rlz[kMaxRlzLength + 1];
      if (GetAccessPointRlz(access_points[i], rlz, arraysize(rlz))) {
        const char* access_point = GetAccessPointName(access_points[i]);
        if (!access_point)
          continue;

        base::StringAppendF(&cgi_string, "%s%s%s%s",
                            first_rlz ? "" : kRlzCgiSeparator,
                            access_point, kRlzCgiIndicator, rlz);
        first_rlz = false;
      }
    }

#if defined(OS_WIN)
    // Report the DCC too if not empty. DCCs are windows-only.
    char dcc[kMaxDccLength + 1];
    dcc[0] = 0;
    if (GetMachineDealCode(dcc, arraysize(dcc)) && dcc[0])
      base::StringAppendF(&cgi_string, "&%s=%s", kDccCgiVariable, dcc);
#endif
  }

  if (cgi_string.size() >= cgi_size)
    return false;

  strncpy(cgi, cgi_string.c_str(), cgi_size);
  cgi[cgi_size - 1] = 0;

  return true;
}

}  // namespace rlz_lib
