#include "engine/private/service/MeetingEventSubscriber.h"

#include "engine/private/service/ClientIpResolver.h"
#include "engine/private/service/IServiceDataCallback.h"

#include <iostream>

namespace VCE {
namespace {

ServiceUserInfo FromProto(const meeting::server::v1::UserInfo &proto) {
  ServiceUserInfo user;
  user.user_id = proto.user_id();
  user.username = proto.username();
  user.client_ip = proto.client_ip();
  return user;
}

void FillProtoUser(const ServiceUserInfo &user,
                   meeting::server::v1::UserInfo *proto) {
  if (!proto) {
    return;
  }

  proto->set_user_id(user.user_id);
  proto->set_username(user.username);
  proto->set_client_ip(ResolveClientIp(user.client_ip));
}

} // namespace

MeetingEventSubscriber::~MeetingEventSubscriber() {
  Stop();
}

void MeetingEventSubscriber::SetServiceDataCallback(
    const std::shared_ptr<IServiceDataCallback> &callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_callback = callback;
}

Result MeetingEventSubscriber::Start(MeetingStub *stub,
                                     const ServiceUserInfo &user,
                                     const std::string &meeting_id) {
  if (!stub) {
    return Result::kRet_NotInitialized;
  }
  if (user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  Stop();

  auto context = std::make_shared<grpc::ClientContext>();
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_context = context;
    m_local_user_id = user.user_id;
    m_meeting_id = meeting_id;
    std::cerr << "[MEETING-EVENT] start subscribe, meeting_id="
              << meeting_id << ", user_id=" << user.user_id << '\n';
    m_thread = std::thread(&MeetingEventSubscriber::SubscribeThreadFunc,
                           this,
                           stub,
                           user,
                           meeting_id,
                           context);
  }
  return Result::kRet_Success;
}

void MeetingEventSubscriber::Stop() {
  std::thread thread;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_context) {
      m_context->TryCancel();
    }
    if (m_thread.joinable()) {
      thread = std::move(m_thread);
    }
  }

  if (thread.joinable()) {
    if (thread.get_id() == std::this_thread::get_id()) {
      thread.detach();
    } else {
      thread.join();
    }
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_context.reset();
  m_local_user_id.clear();
  m_meeting_id.clear();
}

void MeetingEventSubscriber::SubscribeThreadFunc(
    MeetingStub *stub,
    ServiceUserInfo user,
    std::string meeting_id,
    std::shared_ptr<grpc::ClientContext> context) {
  if (!stub || !context) {
    return;
  }

  meeting::server::v1::SubscribeMeetingEventsRequest request;
  FillProtoUser(user, request.mutable_user());
  request.set_meeting_id(meeting_id);

  std::cerr << "[MEETING-EVENT] subscribe thread running, meeting_id="
            << meeting_id << ", user_id=" << user.user_id << '\n';
  auto reader = stub->SubscribeMeetingEvents(context.get(), request);
  meeting::server::v1::MeetingEvent event;
  while (reader && reader->Read(&event)) {
    std::cerr << "[MEETING-EVENT] received event, type="
              << event.type() << ", meeting_id=" << event.meeting_id()
              << ", user_id=" << event.user().user_id() << '\n';
    DispatchMeetingEvent(event);
  }

  if (reader) {
    const auto status = reader->Finish();
    std::cerr << "[MEETING-EVENT] subscribe finished, ok="
              << status.ok() << ", code=" << status.error_code()
              << ", message=" << status.error_message() << '\n';
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_context == context) {
    m_context.reset();
  }
}

void MeetingEventSubscriber::DispatchMeetingEvent(
    const meeting::server::v1::MeetingEvent &event) {
  std::shared_ptr<IServiceDataCallback> callback;
  std::string local_user_id;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback = m_callback.lock();
    local_user_id = m_local_user_id;
  }

  if (!callback) {
    return;
  }

  const auto user = FromProto(event.user());
  switch (event.type()) {
    case meeting::server::v1::USER_JOINED:
      if (!user.user_id.empty() && user.user_id != local_user_id) {
        callback->OnUserJoined(user);
      }
      break;
    case meeting::server::v1::USER_LEFT:
      if (!user.user_id.empty() && user.user_id != local_user_id) {
        callback->OnUserLeft(user.user_id);
      }
      break;
    case meeting::server::v1::MEETING_ENDED:
      if (user.user_id != local_user_id) {
        callback->OnMeetingEnded();
      }
      break;
    default:
      break;
  }
}

} // namespace VCE
