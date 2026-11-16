// Drive the real frame writer and public move/resize event handlers against a
// synthetic AX window. No native windows, daemon or scripting addition is used.
static struct {
    struct application application;
    struct window window;
    struct window_node node;
    struct view view;
    CGRect actual;
    bool refuse_writes;
    bool fullscreen;
    bool visible;
    int pending;
} frame_test;

static AXError test_frame_ax_copy(AXUIElementRef element, CFStringRef attribute, CFTypeRef *value)
{
    *value = NULL;
    if (element == frame_test.window.ref && CFEqual(attribute, kAXPositionAttribute)) {
        *value = AXValueCreate(kAXValueTypeCGPoint, &frame_test.actual.origin);
    } else if (element == frame_test.window.ref && CFEqual(attribute, kAXSizeAttribute)) {
        *value = AXValueCreate(kAXValueTypeCGSize, &frame_test.actual.size);
    } else if (element == frame_test.window.ref && CFEqual(attribute, kAXFullscreenAttribute)) {
        *value = CFRetain(frame_test.fullscreen ? kCFBooleanTrue : kCFBooleanFalse);
    } else if (element == frame_test.application.ref && CFEqual(attribute, kAXEnhancedUserInterface)) {
        *value = CFRetain(kCFBooleanFalse);
    }
    return *value ? kAXErrorSuccess : kAXErrorAttributeUnsupported;
}

static AXError test_frame_ax_set(AXUIElementRef element, CFStringRef attribute, CFTypeRef value)
{
    // Never fall through to real AX writes, even for an unexpected reference.
    if (element != frame_test.window.ref) return kAXErrorInvalidUIElement;
    if (CFEqual(attribute, kAXPositionAttribute)) {
        if (!frame_test.refuse_writes) AXValueGetValue(value, kAXValueTypeCGPoint, &frame_test.actual.origin);
    } else if (CFEqual(attribute, kAXSizeAttribute)) {
        if (!frame_test.refuse_writes) AXValueGetValue(value, kAXValueTypeCGSize, &frame_test.actual.size);
    } else {
        return kAXErrorAttributeUnsupported;
    }
    // Model queued/coalesced notifications, including successful AX requests
    // that leave the frame unchanged (as observed in the November 16 capture).
    ++frame_test.pending;
    return kAXErrorSuccess;
}

CFStringRef test_frame_display(int connection, uint64_t sid)
{
    return CFRetain(CFSTR("00000000-0000-0000-0000-000000000001"));
}
uint32_t test_frame_display_id(CFUUIDRef uuid) { return 1; }
CFUUIDRef test_frame_display_uuid(uint32_t did)
{
    return CFUUIDCreateFromString(NULL, CFSTR("00000000-0000-0000-0000-000000000001"));
}
uint64_t test_frame_current_space(int connection, CFStringRef uuid)
{
    return frame_test.visible ? frame_test.view.sid : frame_test.view.sid + 1;
}

static void frame_test_begin(CGRect actual, struct area target)
{
    memset(&frame_test, 0, sizeof(frame_test));
    memset(&g_window_manager, 0, sizeof(g_window_manager));
    memset(&g_mouse_state, 0, sizeof(g_mouse_state));
    table_init(&g_window_manager.window, 4, hash_wm, compare_wm);
    table_init(&g_window_manager.managed_window, 4, hash_wm, compare_wm);
    if (!g_temp_storage.memory) assert(ts_init(1024 * 1024));
    g_temp_storage.used = 0;
    frame_test.actual = actual;
    frame_test.visible = true;
    frame_test.application.name = "Test application";
    frame_test.application.ref = (AXUIElementRef)CFSTR("test-application");
    frame_test.window.application = &frame_test.application;
    frame_test.window.ref = (AXUIElementRef)CFSTR("test-window");
    frame_test.window.id = 4799;
    frame_test.window.id_ptr = &frame_test.window.id;
    frame_test.window.frame = actual;
    frame_test.node.area = target;
    frame_test.node.window_list[0] = frame_test.window.id;
    frame_test.node.window_count = 1;
    frame_test.view.root = &frame_test.node;
    frame_test.view.sid = 5;
    frame_test.view.layout = VIEW_BSP;
    table_add(&g_window_manager.window, &frame_test.window.id, &frame_test.window);
    table_add(&g_window_manager.managed_window, &frame_test.window.id, &frame_test.view);
}

static void frame_test_event(bool moved)
{
    void *id = (void *)(uintptr_t)frame_test.window.id;
    if (moved) EVENT_HANDLER_WINDOW_MOVED(id, 0);
    else EVENT_HANDLER_WINDOW_RESIZED(id, 0);
}

static bool frame_test_drain(void)
{
    // A refusing window must settle rather than feed an endless resize loop.
    for (int i = 0; frame_test.pending && i < 100; ++i) {
        --frame_test.pending;
        frame_test_event(false);
    }
    return frame_test.pending == 0;
}

static void frame_test_end(void)
{
    table_free(&g_window_manager.window);
    table_free(&g_window_manager.managed_window);
    munmap(g_temp_storage.memory, g_temp_storage.size + getpagesize());
    memset(&g_temp_storage, 0, sizeof(g_temp_storage));
}

TEST_FUNC(managed_window_frame_recovery,
{
    CGRect startup = CGRectMake(642, 34, 634, 794);
    CGRect target = CGRectMake(193, 34, 1082, 794);
    frame_test_begin(startup, ((struct area) {193, 34, 1082, 794}));
    window_manager_set_window_frame(&frame_test.window, 193, 34, 1082, 794);
    TEST_CHECK(frame_test_drain(), true);
    // Replay both external reversions before the successful write's queued
    // event refreshes the cache, as in the November 15 incident.
    frame_test.actual = startup;
    frame_test_event(false);
    frame_test.actual = startup;
    frame_test_event(false);
    TEST_CHECK(CGRectEqualToRect(frame_test.actual, target), true);
    TEST_CHECK(frame_test_drain(), true);
    TEST_CHECK(CGRectEqualToRect(frame_test.actual, target), true);
    // Once a correct frame has been observed, another restoration can recover.
    frame_test.actual = startup;
    frame_test_event(true);
    TEST_CHECK(frame_test_drain(), true);
    TEST_CHECK(CGRectEqualToRect(frame_test.actual, target), true);
    frame_test_end();

    // Initial AX writes return success but do not apply, as on November 16.
    for (int moved = 0; moved < 2; ++moved) {
        startup = CGRectMake(644, 282, 632, 794);
        target = CGRectMake(962, 35, 954, 1041);
        frame_test_begin(startup, ((struct area) {962, 35, 954, 1041}));
        frame_test.refuse_writes = true;
        window_manager_set_window_frame(&frame_test.window, 962, 35, 954, 1041);
        frame_test.refuse_writes = false;
        frame_test_event(moved);
        TEST_CHECK(frame_test_drain(), true);
        TEST_CHECK(CGRectEqualToRect(frame_test.actual, target), true);
        frame_test_end();
    }

    // Permanently refused/minimum-size-constrained writes must quiesce. A
    // changed layout target can subsequently recover without restarting yabai.
    frame_test_begin(startup, ((struct area) {962, 35, 954, 1041}));
    frame_test.refuse_writes = true;
    frame_test.pending = 1;
    TEST_CHECK(frame_test_drain(), true);
    TEST_CHECK(CGRectEqualToRect(frame_test.actual, startup), true);
    frame_test.node.area = ((struct area) {10, 40, 900, 800});
    frame_test.refuse_writes = false;
    frame_test_event(false);
    TEST_CHECK(frame_test_drain(), true);
    TEST_CHECK(CGRectEqualToRect(frame_test.actual, CGRectMake(10, 40, 900, 800)), true);
    frame_test_end();
});

TEST_FUNC(managed_window_frame_recovery_safeguards,
{
    CGRect startup = CGRectMake(642, 34, 634, 794);
    for (int mode = 0; mode < 10; ++mode) {
        frame_test_begin(startup, ((struct area) {193, 34, 1082, 794}));
        struct window_node zoom = {0};
        zoom.area = ((struct area) {642, 34, 634, 794});
        switch (mode) {
        case 0: frame_test.application.is_hidden = true; break;
        case 1: g_mouse_state.window = &frame_test.window; break;
        case 2: table_remove(&g_window_manager.managed_window, &frame_test.window.id); break;
        case 3: frame_test.fullscreen = true; break;
        case 4: frame_test.window.windowed_frame = startup; break;
        case 5: frame_test.node.zoom = &zoom; break;
        case 6: window_set_flag(&frame_test.window, WINDOW_MINIMIZE); break;
        case 7: frame_test.node.area = ((struct area) {643, 35, 635, 795}); break;
        case 8: window_set_flag(&frame_test.window, WINDOW_FULLSCREEN); break;
        case 9: frame_test.window.id_ptr = NULL; break;
        }
        frame_test_event(false);
        frame_test_event(true);
        TEST_CHECK(frame_test_drain(), true);
        TEST_CHECK(CGRectEqualToRect(frame_test.actual, startup), true);
        frame_test_end();
    }

    // At the existing 1.5-point threshold, correction is required.
    frame_test_begin(startup, ((struct area) {643.5, 34, 634, 794}));
    frame_test_event(false);
    TEST_CHECK(frame_test_drain(), true);
    TEST_CHECK(CGRectEqualToRect(frame_test.actual, CGRectMake(643.5, 34, 634, 794)), true);
    frame_test_end();

    // If zoom is active but neither allocation matches, use the zoom target.
    frame_test_begin(startup, ((struct area) {193, 34, 1082, 794}));
    struct window_node zoom = {0};
    zoom.area = ((struct area) {4, 34, 1272, 794});
    frame_test.node.zoom = &zoom;
    frame_test_event(false);
    TEST_CHECK(frame_test_drain(), true);
    TEST_CHECK(CGRectEqualToRect(frame_test.actual, CGRectMake(4, 34, 1272, 794)), true);
    frame_test_end();

    frame_test_begin(startup, ((struct area) {193, 34, 1082, 794}));
    frame_test.visible = false;
    frame_test_event(false);
    TEST_CHECK(CGRectEqualToRect(frame_test.actual, startup), true);
    TEST_CHECK(!!view_check_flag(&frame_test.view, VIEW_IS_DIRTY), true);
    frame_test_end();
});
