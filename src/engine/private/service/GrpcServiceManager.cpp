#include "engine/private/service/GrpcServiceManager.h"

#include <cstdlib>
#include <utility>

#include "engine/private/service/ClientIpResolver.h"
#include "engine/private/service/IServiceDataCallback.h"

namespace VCE {
namespace {

constexpr char kDefaultMeetingServerAddress[] = "127.0.0.1:50051";

bool IsBlank(const std::string &value) {
  return value.empty();
}

} // namespace

GrpcServiceManager::GrpcServiceManager()
    : GrpcServiceManager(ResolveServerAddress()) {}

GrpcServiceManager::GrpcServiceManager(
    std::string server_address, std::chrono::milliseconds rpc_timeout)
    : m_server_address(std::move(server_address)),
      m_rpc_timeout(rpc_timeout) {}

GrpcServiceManager::~GrpcServiceManager() {
  m_event_subscriber.Stop();
}

Result GrpcServiceManager::Init() {
  if (m_initialized) {
    return Result::kRet_AlreadyInitialized;
  }

  if (m_server_address.empty() || m_rpc_timeout.count() <= 0) {
    return Result::kRet_InvalidArgument;
  }

  m_channel = grpc::CreateChannel(
      m_server_address, grpc::InsecureChannelCredentials());
  m_user_stub = meeting::server::v1::UserService::NewStub(m_channel);
  m_meeting_stub = meeting::server::v1::MeetingService::NewStub(m_channel);
  m_initialized = true;
  return Result::kRet_Success;
}

Result GrpcServiceManager::Uninit() {
  m_event_subscriber.Stop();
  m_meeting_stub.reset();
  m_user_stub.reset();
  m_channel.reset();
  m_callback.reset();
  m_initialized = false;
  return Result::kRet_Success;
}

void GrpcServiceManager::SetServiceDataCallback(
    const std::shared_ptr<IServiceDataCallback> &callback) {
  m_callback = callback;
  m_event_subscriber.SetServiceDataCallback(callback);
}

Result GrpcServiceManager::RegisterUser(const std::string &username,
                                        const std::string &password,
                                        uint32_t client_ip,
                                        ServiceUserInfo *user_info) {
  if (!m_initialized || !m_user_stub) {
    return Result::kRet_NotInitialized;
  }

  if (IsBlank(username) || IsBlank(password)) {
    return Result::kRet_InvalidArgument;
  }

  meeting::server::v1::RegisterRequest request;
  request.set_username(username);
  request.set_password(password);
  request.set_client_ip(ResolveClientIp(client_ip));

  meeting::server::v1::RegisterResponse response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + m_rpc_timeout);
  const auto status = m_user_stub->Register(&context, request, &response);
  const auto result = ValidateResponse(response.error_code(), status);
  if (result != Result::kRet_Success) {
    return result;
  }

  if (user_info) {
    *user_info = FromProto(response.user());
  }
  return Result::kRet_Success;
}

Result GrpcServiceManager::Login(const std::string &username,
                                 const std::string &password,
                                 uint32_t client_ip,
                                 ServiceUserInfo *user_info) {
  if (!m_initialized || !m_user_stub) {
    return Result::kRet_NotInitialized;
  }

  if (IsBlank(username) || IsBlank(password)) {
    return Result::kRet_InvalidArgument;
  }

  meeting::server::v1::LoginRequest request;
  request.set_username(username);
  request.set_password(password);
  request.set_client_ip(ResolveClientIp(client_ip));

  meeting::server::v1::LoginResponse response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + m_rpc_timeout);
  const auto status = m_user_stub->Login(&context, request, &response);
  const auto result = ValidateResponse(response.error_code(), status);
  if (result != Result::kRet_Success) {
    return result;
  }

  if (user_info) {
    *user_info = FromProto(response.user());
  }
  return Result::kRet_Success;
}

Result GrpcServiceManager::CreateMeeting(const ServiceUserInfo &creator,
                                         const std::string &title,
                                         const std::string &description,
                                         int64_t start_time,
                                         MeetingInfo *meeting_info) {
  if (!m_initialized || !m_meeting_stub) {
    return Result::kRet_NotInitialized;
  }

  if (creator.user_id.empty() || title.empty()) {
    return Result::kRet_InvalidArgument;
  }

  meeting::server::v1::CreateMeetingRequest request;
  FillProtoUser(creator, request.mutable_creator());
  request.set_title(title);
  request.set_description(description);
  request.set_start_time(start_time);

  meeting::server::v1::CreateMeetingResponse response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + m_rpc_timeout);
  const auto status =
      m_meeting_stub->CreateMeeting(&context, request, &response);
  const auto result = ValidateResponse(response.error_code(), status);
  if (result != Result::kRet_Success) {
    return result;
  }

  if (meeting_info) {
    *meeting_info = FromProto(response.meeting());
  }
  m_event_subscriber.Start(m_meeting_stub.get(),
                           creator,
                           response.meeting().meeting_id());
  return Result::kRet_Success;
}

Result GrpcServiceManager::JoinMeeting(const ServiceUserInfo &user,
                                       const std::string &meeting_id,
                                       MeetingInfo *meeting_info) {
  if (!m_initialized || !m_meeting_stub) {
    return Result::kRet_NotInitialized;
  }

  if (user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  meeting::server::v1::JoinMeetingRequest request;
  FillProtoUser(user, request.mutable_user());
  request.set_meeting_id(meeting_id);

  meeting::server::v1::JoinMeetingResponse response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + m_rpc_timeout);
  const auto status = m_meeting_stub->JoinMeeting(&context, request, &response);
  const auto result = ValidateResponse(response.error_code(), status);
  if (result != Result::kRet_Success) {
    return result;
  }

  if (meeting_info) {
    *meeting_info = FromProto(response.meeting());
  }
  m_event_subscriber.Start(m_meeting_stub.get(),
                           user,
                           response.meeting().meeting_id());
  return Result::kRet_Success;
}

Result GrpcServiceManager::LeaveMeeting(
    const ServiceUserInfo &user,
    const std::string &meeting_id,
    std::vector<ServiceUserInfo> *participants) {
  if (!m_initialized || !m_meeting_stub) {
    return Result::kRet_NotInitialized;
  }

  if (user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  meeting::server::v1::LeaveMeetingRequest request;
  FillProtoUser(user, request.mutable_user());
  request.set_meeting_id(meeting_id);

  meeting::server::v1::LeaveMeetingResponse response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + m_rpc_timeout);
  const auto status =
      m_meeting_stub->LeaveMeeting(&context, request, &response);
  const auto result = ValidateResponse(response.error_code(), status);
  if (result != Result::kRet_Success) {
    return result;
  }

  FillParticipants(response.participants(), participants);
  m_event_subscriber.Stop();
  return Result::kRet_Success;
}

Result GrpcServiceManager::EndMeeting(
    const ServiceUserInfo &user,
    const std::string &meeting_id,
    std::vector<ServiceUserInfo> *participants) {
  if (!m_initialized || !m_meeting_stub) {
    return Result::kRet_NotInitialized;
  }

  if (user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  meeting::server::v1::EndMeetingRequest request;
  FillProtoUser(user, request.mutable_user());
  request.set_meeting_id(meeting_id);

  meeting::server::v1::EndMeetingResponse response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + m_rpc_timeout);
  const auto status = m_meeting_stub->EndMeeting(&context, request, &response);
  const auto result = ValidateResponse(response.error_code(), status);
  if (result != Result::kRet_Success) {
    return result;
  }

  FillParticipants(response.participants(), participants);
  m_event_subscriber.Stop();
  if (auto callback = m_callback.lock()) {
    callback->OnMeetingEnded();
  }
  return Result::kRet_Success;
}

Result GrpcServiceManager::ListMeetings(
    bool active_only,
    std::vector<MeetingInfo> *meetings) {
  if (!m_initialized || !m_meeting_stub) {
    return Result::kRet_NotInitialized;
  }

  meeting::server::v1::ListMeetingsRequest request;
  request.set_active_only(active_only);

  meeting::server::v1::ListMeetingsResponse response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + m_rpc_timeout);
  const auto status =
      m_meeting_stub->ListMeetings(&context, request, &response);
  const auto result = ValidateResponse(response.error_code(), status);
  if (result != Result::kRet_Success) {
    return result;
  }

  FillMeetings(response.meetings(), meetings);
  return Result::kRet_Success;
}

const std::string &GrpcServiceManager::GetServerAddress() const {
  return m_server_address;
}

std::string GrpcServiceManager::ResolveServerAddress() {
  if (const char *value = std::getenv("MEETING_SERVER_ADDRESS")) {
    if (*value != '\0') {
      return value;
    }
  }
  return kDefaultMeetingServerAddress;
}

Result GrpcServiceManager::ResultFromGrpcStatus(const grpc::Status &status) {
  if (status.ok()) {
    return Result::kRet_Success;
  }

  switch (status.error_code()) {
    case grpc::StatusCode::INVALID_ARGUMENT:
    case grpc::StatusCode::ALREADY_EXISTS:
    case grpc::StatusCode::NOT_FOUND:
    case grpc::StatusCode::UNAUTHENTICATED:
      return Result::kRet_InvalidArgument;
    case grpc::StatusCode::UNAVAILABLE:
    case grpc::StatusCode::DEADLINE_EXCEEDED:
      return Result::kRet_InvalidState;
    default:
      return Result::kRet_InternalError;
  }
}

Result GrpcServiceManager::ResultFromBusinessCode(uint32_t error_code) {
  switch (error_code) {
    case 0:
      return Result::kRet_Success;
    case 1:
    case 2:
    case 3:
    case 4:
      return Result::kRet_InvalidArgument;
    default:
      return Result::kRet_InternalError;
  }
}

ServiceUserInfo GrpcServiceManager::FromProto(
    const meeting::server::v1::UserInfo &proto) {
  ServiceUserInfo user;
  user.user_id = proto.user_id();
  user.username = proto.username();
  user.client_ip = proto.client_ip();
  return user;
}

void GrpcServiceManager::FillProtoUser(
    const ServiceUserInfo &user,
    meeting::server::v1::UserInfo *proto) {
  if (!proto) {
    return;
  }

  proto->set_user_id(user.user_id);
  proto->set_username(user.username);
  proto->set_client_ip(ResolveClientIp(user.client_ip));
}

MeetingMediaRoute GrpcServiceManager::FromProto(
    const meeting::server::v1::MediaRoute &proto) {
  MeetingMediaRoute route;
  route.push_server_url = proto.push_server_url();
  route.pull_server_url = proto.pull_server_url();
  route.srs_app = proto.srs_app().empty() ? "meeting" : proto.srs_app();
  route.srs_secret = proto.srs_secret();
  route.stream_prefix = proto.stream_prefix();
  return route;
}

MeetingInfo GrpcServiceManager::FromProto(
    const meeting::server::v1::MeetingInfo &proto) {
  MeetingInfo meeting;
  meeting.meeting_id = proto.meeting_id();
  meeting.title = proto.title();
  meeting.description = proto.description();
  meeting.creator = FromProto(proto.creator());
  meeting.active = proto.active();
  meeting.media_route = FromProto(proto.media_route());
  FillParticipants(proto.participants(), &meeting.participants);
  return meeting;
}

void GrpcServiceManager::FillParticipants(
    const google::protobuf::RepeatedPtrField<
        meeting::server::v1::UserInfo> &proto_participants,
    std::vector<ServiceUserInfo> *participants) {
  if (!participants) {
    return;
  }

  participants->clear();
  participants->reserve(static_cast<size_t>(proto_participants.size()));
  for (const auto &participant : proto_participants) {
    participants->push_back(FromProto(participant));
  }
}

void GrpcServiceManager::FillMeetings(
    const google::protobuf::RepeatedPtrField<
        meeting::server::v1::MeetingInfo> &proto_meetings,
    std::vector<MeetingInfo> *meetings) {
  if (!meetings) {
    return;
  }

  meetings->clear();
  meetings->reserve(static_cast<size_t>(proto_meetings.size()));
  for (const auto &meeting : proto_meetings) {
    meetings->push_back(FromProto(meeting));
  }
}

Result GrpcServiceManager::ValidateResponse(uint32_t error_code,
                                            const grpc::Status &status) {
  if (!status.ok()) {
    return ResultFromGrpcStatus(status);
  }
  return ResultFromBusinessCode(error_code);
}

} // namespace VCE
