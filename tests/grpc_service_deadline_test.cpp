#include "engine/private/service/GrpcServiceManager.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace proto = meeting::server::v1;
using namespace std::chrono;
namespace {
bool passed = true;
void Check(bool ok, const char* message) {
  if (!ok) { passed = false; std::cerr << "FAIL: " << message << '\n'; }
}
class SlowMeetingService : public proto::MeetingService::Service {
public:
  std::atomic<bool> slow{true};
  std::atomic<int> calls{0};
  grpc::Status ListMeetings(grpc::ServerContext* context,
      const proto::ListMeetingsRequest*, proto::ListMeetingsResponse* response) override {
    ++calls;
    if (slow.load()) {
      const auto end = steady_clock::now() + seconds(3);
      while (!context->IsCancelled() && steady_clock::now() < end)
        std::this_thread::sleep_for(milliseconds(5));
      return grpc::Status(grpc::StatusCode::CANCELLED, "slow test request");
    }
    response->add_meetings()->set_meeting_id("recovered");
    return grpc::Status::OK;
  }
};
}
int main() {
  SlowMeetingService service;
  grpc::ServerBuilder builder;
  int port = 0;
  builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
  builder.RegisterService(&service);
  auto server = builder.BuildAndStart();
  if (!server || !port) return 1;
  const auto address = "127.0.0.1:" + std::to_string(port);
  VCE::GrpcServiceManager invalid(address, milliseconds(0));
  Check(invalid.Init() == VCE::Result::kRet_InvalidArgument, "reject nonpositive timeout");
  VCE::GrpcServiceManager manager(address, milliseconds(200));
  Check(manager.Init() == VCE::Result::kRet_Success, "init");
  std::vector<VCE::MeetingInfo> meetings(1);
  meetings[0].meeting_id = "unchanged";
  const auto start = steady_clock::now();
  const auto result = manager.ListMeetings(true, &meetings);
  const auto elapsed = steady_clock::now() - start;
  Check(service.calls.load() > 0, "request reached slow server");
  Check(result == VCE::Result::kRet_InvalidState, "deadline mapped to existing error result");
  Check(elapsed < seconds(2), "request does not wait for three-second server delay");
  Check(meetings.size() == 1 && meetings[0].meeting_id == "unchanged", "failure leaves output unchanged");
  service.slow.store(false);
  Check(manager.ListMeetings(true, &meetings) == VCE::Result::kRet_Success, "request succeeds after recovery");
  Check(meetings.size() == 1 && meetings[0].meeting_id == "recovered", "recovered output");
  Check(manager.Uninit() == VCE::Result::kRet_Success, "shutdown");
  server->Shutdown();
  server->Wait();
  std::cout << "Slow RPC elapsed ms: " << duration_cast<milliseconds>(elapsed).count() << '\n';
  return passed ? 0 : 1;
}
