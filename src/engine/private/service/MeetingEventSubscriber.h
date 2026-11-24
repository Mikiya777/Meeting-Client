#ifndef MEETING_EVENT_SUBSCRIBER_H
#define MEETING_EVENT_SUBSCRIBER_H

#include "engine/public/VceTypes.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "meeting_service.grpc.pb.h"

namespace VCE {

class IServiceDataCallback;

class MeetingEventSubscriber final {
public:
  using MeetingStub = meeting::server::v1::MeetingService::Stub;

  MeetingEventSubscriber() = default;
  ~MeetingEventSubscriber();

  void SetServiceDataCallback(
      const std::shared_ptr<IServiceDataCallback> &callback);
  Result Start(MeetingStub *stub,
               const ServiceUserInfo &user,
               const std::string &meeting_id);
  void Stop();

private:
  void SubscribeThreadFunc(MeetingStub *stub,
                           ServiceUserInfo user,
                           std::string meeting_id,
                           std::shared_ptr<grpc::ClientContext> context);
  void DispatchMeetingEvent(
      const meeting::server::v1::MeetingEvent &event);

  std::mutex m_mutex;
  std::weak_ptr<IServiceDataCallback> m_callback;
  std::shared_ptr<grpc::ClientContext> m_context;
  std::string m_local_user_id;
  std::string m_meeting_id;
  std::thread m_thread;
};

} // namespace VCE

#endif // MEETING_EVENT_SUBSCRIBER_H
