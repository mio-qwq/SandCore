/* 每个函数正文仅在运行期独立SC32API.SCL中出现，不复制进应用SCX。
 * 单翻译单元满足当前SCCC工具链；各片段仍按系统对象职责分开。 */
static void sc32_gui_finalize(void);
#include "sc32_bridge.c"
#include "sc32_support.c"
#include "sc32_system.c"
#include "sc32_process.c"
#include "sc32_path.c"
#include "sc32_objects.c"
#include "sc32_file.c"
#include "../guiapi/sc32_gui.c"
#include "../guiapi/sc32_popup.c"
#include "../fontapi/sc32_font.c"
#include "../fontapi/sc32_palette.c"
#include "../brokerapi/sc32_broker.c"
#include "../dropapi/sc32_drop.c"
