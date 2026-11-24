#include "transport/include/ITransportEngine.h"
#include "transport/src/local/LocalTransportEngine.h"
#include "transport/src/srs/SrsTransportEngine.h"

#include <cstdlib>
#include <memory>
#include <string>

namespace TRANSPORT {
namespace {

/*
  1.默认创建LocalTransportEngine
  2.当环境变量 MEETING_TRANSPORT_ENGINE=srs/SRS 时，创建 SrsTransportEngine
  3.默认不走 SRS，是为了让本地开发和单元测试不依赖外部服务器、网络、SRS 配置、编解码库运行状态。这样项目更稳定
  */
bool UseSrsTransportEngine() {
  const char *value = std::getenv("MEETING_TRANSPORT_ENGINE");
  if (!value) {
    return false;
  }

  const std::string engine_name(value);
  return engine_name == "srs" || engine_name == "SRS";
}

} // namespace

/*
  通过基类智能指针持有具体子类对象，实现运行时多态。
*/
std::unique_ptr<ITransportEngine> ITransportEngine::Create() {
  if (UseSrsTransportEngine()) {
    return std::make_unique<SrsTransportEngine>();
  }

  return std::make_unique<LocalTransportEngine>();
}

} // namespace TRANSPORT
