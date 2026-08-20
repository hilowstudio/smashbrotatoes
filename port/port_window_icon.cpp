#include "port_window_icon.h"

namespace ssb64 {
// Windows uses .ico via ssb64.rc, which fires before the app runs, so no
// SDL-level intervention is needed.
void SetWindowIcon() {}
}  // namespace ssb64
