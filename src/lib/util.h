#pragma once

#include <string_view>

#include <QString>
#include <qcorotask.h>

namespace lgx {

[[nodiscard]] QString applicationVersion();
[[nodiscard]] std::string_view applicationVersionView() noexcept;
[[nodiscard]] QCoro::Task<QString> applicationVersionAsync();

}  // namespace lgx
