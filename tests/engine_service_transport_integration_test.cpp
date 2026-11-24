#include "engine/private/capture/CaptureManager.h"
#include "engine/private/render/MockRenderManager.h"
#include "engine/private/service/MockServiceManager.h"
#include "engine/private/transport/TransportManager.h"
#include "engine/private/VceEngineImpl.h"

#include <iostream>
#include <memory>

namespace {

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    return false;
  }

  std::cout << "[PASSED] " << message << '\n';
  return true;
}

} // namespace

int main() {
  bool passed = true;

  auto engine = std::make_shared<VCE::VceEngineImpl>(
      std::make_unique<VCE::CaptureManager>(),
      std::make_unique<VCE::MockRenderManager>(),
      std::make_unique<VCE::TransportManager>(),
      std::make_unique<VCE::MockServiceManager>());
  passed &= Check(engine->Init() == VCE::Result::kRet_Success,
                  "engine initializes");

  VCE::ServiceUserInfo user;
  passed &= Check(engine->RegisterUser("alice", "password", 0, &user) ==
                      VCE::Result::kRet_Success,
                  "user registration succeeds");
  passed &= Check(engine->Login("alice", "password", 0, &user) ==
                      VCE::Result::kRet_Success,
                  "user login succeeds");
  passed &= Check(engine->GetCurrentUserInfo().user_id == "alice",
                  "engine stores the logged-in user");

  VCE::MeetingInfo meeting;
  passed &= Check(engine->CreateMeeting("Daily Meeting", "integration test",
                                        0, &meeting) ==
                      VCE::Result::kRet_Success,
                  "meeting creation succeeds");
  passed &= Check(!meeting.meeting_id.empty(),
                  "created meeting has a meeting id");
  passed &= Check(meeting.active, "created meeting is active");
  passed &= Check(meeting.participants.size() == 1,
                  "created meeting contains local participant");

  const auto current_meeting = engine->GetCurrentMeetingInfo();
  passed &= Check(current_meeting.meeting_id == meeting.meeting_id,
                  "engine stores current meeting");

  const auto room_info = engine->GetCurrentTransportRoomInfo();
  passed &= Check(room_info.local_user_id == "alice",
                  "transport room uses logged-in user as local user");
  passed &= Check(room_info.room_id == meeting.meeting_id,
                  "transport room uses meeting id as room id");
  passed &= Check(!room_info.push_server_url.empty(),
                  "transport room has a push server url");
  passed &= Check(!room_info.pull_server_url.empty(),
                  "transport room has a pull server url");
  passed &= Check(room_info.srs_app == meeting.media_route.srs_app,
                  "transport room keeps SRS app from service route");

  passed &= Check(engine->LeaveMeeting() == VCE::Result::kRet_Success,
                  "leaving meeting succeeds");
  passed &= Check(engine->GetCurrentMeetingInfo().meeting_id.empty(),
                  "engine clears current meeting after leave");

  passed &= Check(engine->Uninit() == VCE::Result::kRet_Success,
                  "engine uninitializes");

  return passed ? 0 : 1;
}
