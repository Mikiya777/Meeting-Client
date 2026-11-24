#ifndef GRPC_SERVICE_MANAGER_H
#define GRPC_SERVICE_MANAGER_H

#include "engine/private/service/IServiceManager.h"
#include "engine/private/service/MeetingEventSubscriber.h"

#include <chrono>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "meeting_service.grpc.pb.h"
#include "user_service.grpc.pb.h"

namespace VCE {

class GrpcServiceManager final : public IServiceManager {
public:
  GrpcServiceManager();
  explicit GrpcServiceManager(
      std::string server_address,
      std::chrono::milliseconds rpc_timeout = std::chrono::seconds(5));
  ~GrpcServiceManager() override;

  Result Init() override;
  Result Uninit() override;
  void SetServiceDataCallback(
      const std::shared_ptr<IServiceDataCallback> &callback) override;

  Result RegisterUser(const std::string &username,
                      const std::string &password,
                      uint32_t client_ip,
                      ServiceUserInfo *user_info) override;
  Result Login(const std::string &username,
               const std::string &password,
               uint32_t client_ip,
               ServiceUserInfo *user_info) override;

  Result CreateMeeting(const ServiceUserInfo &creator,
                       const std::string &title,
                       const std::string &description,
                       int64_t start_time,
                       MeetingInfo *meeting_info) override;
  Result JoinMeeting(const ServiceUserInfo &user,
                     const std::string &meeting_id,
                     MeetingInfo *meeting_info) override;
  Result LeaveMeeting(const ServiceUserInfo &user,
                      const std::string &meeting_id,
                      std::vector<ServiceUserInfo> *participants) override;
  Result EndMeeting(const ServiceUserInfo &user,
                    const std::string &meeting_id,
                    std::vector<ServiceUserInfo> *participants) override;
  Result ListMeetings(bool active_only,
                      std::vector<MeetingInfo> *meetings) override;

  const std::string &GetServerAddress() const;

private:
  static std::string ResolveServerAddress();
  static Result ResultFromGrpcStatus(const grpc::Status &status);
  static Result ResultFromBusinessCode(uint32_t error_code);
  static ServiceUserInfo FromProto(
      const meeting::server::v1::UserInfo &proto);
  static void FillProtoUser(const ServiceUserInfo &user,
                            meeting::server::v1::UserInfo *proto);
  static MeetingMediaRoute FromProto(
      const meeting::server::v1::MediaRoute &proto);
  static MeetingInfo FromProto(
      const meeting::server::v1::MeetingInfo &proto);
  static void FillParticipants(
      const google::protobuf::RepeatedPtrField<
          meeting::server::v1::UserInfo> &proto_participants,
      std::vector<ServiceUserInfo> *participants);
  static void FillMeetings(
      const google::protobuf::RepeatedPtrField<
          meeting::server::v1::MeetingInfo> &proto_meetings,
      std::vector<MeetingInfo> *meetings);
  static Result ValidateResponse(uint32_t error_code,
                                 const grpc::Status &status);

private:
  std::string m_server_address;
  // Per unary RPC; the long-lived event subscription uses explicit cancellation.
  std::chrono::milliseconds m_rpc_timeout;
  std::shared_ptr<grpc::Channel> m_channel;
  std::unique_ptr<meeting::server::v1::UserService::Stub> m_user_stub;
  std::unique_ptr<meeting::server::v1::MeetingService::Stub> m_meeting_stub;
  std::weak_ptr<IServiceDataCallback> m_callback;
  MeetingEventSubscriber m_event_subscriber;
  bool m_initialized = false;
};

} // namespace VCE

#endif // GRPC_SERVICE_MANAGER_H
