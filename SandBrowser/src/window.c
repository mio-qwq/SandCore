/* Copyright (c) 2026 mio. 完整帧的 SC32 绘制事务，安装器与浏览器共用。 */
#include "sb.h"

ScStatus sb_window_present(ScWindow window, const uint32_t *pixels, unsigned width,
                           unsigned height) {
    if (!pixels || !width || !height || width > 1920 || height > 1080)
        return SC32_E_RANGE;
    ScWindowInfo info;
    sb_record(&info, sizeof(info));
    ScStatus status = ScWindowQuery(window, &info);
    if (status != SC32_OK)
        return status;
    if (info.client_width != width || info.client_height != height)
        return SC32_E_STALE;
    if (!(info.state & (SC32_WINDOW_VISIBLE | SC32_WINDOW_PREPARING)) ||
        (info.state & SC32_WINDOW_MINIMIZED))
        return SC32_PENDING;

    /* 每次上传都是新画好的完整帧。先登记损伤，再领取当前 geometry 的
     * paint claim；最小化时不上传，恢复后的首帧仍由待办损伤驱动。 */
    status = ScWindowInvalidate(window, 0, 0, width, height);
    if (status != SC32_OK)
        return status;
    ScPaintInfo paint;
    sb_record(&paint, sizeof(paint));
    status = ScPaintBegin(window, &paint);
    if (status != SC32_OK)
        return status;
    if (paint.client_width != width || paint.client_height != height) {
        ScPaintCancel(&paint);
        return SC32_E_STALE;
    }
    ScSubmitRect rectangle = {0, 0, 0, 0, width, height};
    ScSubmitDesc submit;
    sb_record(&submit, sizeof(submit));
    submit.window = window;
    submit.generation = paint.generation;
    submit.geometry_epoch = paint.geometry_epoch;
    submit.pixels = pixels;
    submit.bytes = width * height * 4;
    submit.width = width;
    submit.height = height;
    submit.stride = width * 4;
    submit.rectangles = &rectangle;
    submit.rectangle_count = 1;
    status = ScWindowSubmit(&submit);
    if (status == SC32_OK)
        status = ScPaintEnd(&paint);
    /* 失败不得确认损伤；撤回 claim，让恢复/下一帧仍可重画。 */
    if (status != SC32_OK)
        ScPaintCancel(&paint);
    return status;
}
