#include "util.h"

namespace lgx {

QString applicationVersion() {
  return QString::fromLatin1(LGX_VERSION);
}

std::string_view applicationVersionView() noexcept {
  return LGX_VERSION;
}

QCoro::Task<QString> applicationVersionAsync() {
  co_return applicationVersion();
}

}  // namespace lgx
