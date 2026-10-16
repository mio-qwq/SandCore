#ifndef SANDCORE_SCUITABS_H
#define SANDCORE_SCUITABS_H
#include "SCUI.H"
#define SC_UI_TABS_MIN_ABI_MINOR 2u
#define SC_UI_TABS_MIN_LIBRARY_VERSION 3u
/* Remove同时销毁所属页及其子控件；Document的应用引用另行释放。
 * SetText只改标签，保留页/选择/编辑视口与Undo历史。 */
ScStatus ScUiTabRemove(ScUiContext *,ScUiNode tabs,ScUiNode page);
ScStatus ScUiTabSetText(ScUiContext *,ScUiNode tabs,ScUiNode page,const ScUiText *);
#endif
