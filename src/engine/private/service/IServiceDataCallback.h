#ifndef ISERVICEDATACALLBACK_H
#define ISERVICEDATACALLBACK_H

#include "engine/public/VceTypes.h"

#include <string>

namespace VCE {
class IServiceDataCallback {
public:
  virtual ~IServiceDataCallback() = default;

  virtual void OnUserJoined(const ServiceUserInfo &user) = 0;
  virtual void OnUserLeft(const std::string &user_id) = 0;
  virtual void OnMeetingEnded() = 0;
};
} // namespace VCE
#endif //ISERVICEDATACALLBACK_H
