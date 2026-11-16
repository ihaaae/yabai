extern struct event_loop g_event_loop;
extern enum mission_control_mode g_mission_control_mode;
extern volatile uint64_t __last_cmd_tab_time;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static CONNECTION_CALLBACK(connection_handler)
{
    if (type == 1204) {
        event_loop_post(&g_event_loop, MISSION_CONTROL_ENTER, NULL, 0);
    } else if (type == 1327) {
        uint64_t sid; memcpy(&sid, data, sizeof(uint64_t));
        event_loop_post(&g_event_loop, SLS_SPACE_CREATED, (void *) (intptr_t) sid , 0);
    } else if (type == 1328) {
        uint64_t sid; memcpy(&sid, data, sizeof(uint64_t));
        event_loop_post(&g_event_loop, SLS_SPACE_DESTROYED, (void *) (intptr_t) sid, 0);
    } else if (type == 808) {
        uint32_t wid; memcpy(&wid, data, sizeof(uint32_t));
        event_loop_post(&g_event_loop, SLS_WINDOW_ORDERED, (void *) (intptr_t) wid, 0);
    } else if (type == 804) {
        uint32_t wid; memcpy(&wid, data, sizeof(uint32_t));
        event_loop_post(&g_event_loop, SLS_WINDOW_DESTROYED, (void *) (intptr_t) wid, 0);
    } else if (type == 1202) {
        __atomic_store_n(&__last_cmd_tab_time, read_os_timer(), __ATOMIC_RELEASE);
    }
}
#pragma clang diagnostic pop

enum mission_control_mode
{
    MISSION_CONTROL_MODE_INACTIVE           = 0,
    MISSION_CONTROL_MODE_SHOW               = 1,
    MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS   = 2,
    MISSION_CONTROL_MODE_SHOW_FRONT_WINDOWS = 3,
    MISSION_CONTROL_MODE_SHOW_DESKTOP       = 4
};

static const char *mission_control_mode_str[] = {
    [MISSION_CONTROL_MODE_INACTIVE]           = "inactive",
    [MISSION_CONTROL_MODE_SHOW]               = "show",
    [MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS]   = "show-all-windows",
    [MISSION_CONTROL_MODE_SHOW_FRONT_WINDOWS] = "show-front-windows",
    [MISSION_CONTROL_MODE_SHOW_DESKTOP]       = "show-desktop"
};

static struct {
    AXUIElementRef ref;
    AXObserverRef observer_ref;
    bool is_observing;
} g_mission_control_observer;

static CFStringRef kAXExposeShowAllWindows   = CFSTR("AXExposeShowAllWindows");
static CFStringRef kAXExposeShowFrontWindows = CFSTR("AXExposeShowFrontWindows");
static CFStringRef kAXExposeShowDesktop      = CFSTR("AXExposeShowDesktop");
static CFStringRef kAXExposeExit             = CFSTR("AXExposeExit");

static dispatch_source_t g_mission_control_timer;
static bool g_mission_control_surface_visible;
static unsigned g_mission_control_surface_misses;

static void mission_control_poll(void)
{
    CFArrayRef window_list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, 0);
    if (!window_list) return;

    NSArray *applications = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.WindowManager"];
    pid_t pid = applications.count == 1 ? [applications[0] processIdentifier] : 0;
    bool shield = false;
    bool spaces_bar = false;

    for (CFIndex i = 0; pid && i < CFArrayGetCount(window_list); ++i) {
        CFDictionaryRef window = CFArrayGetValueAtIndex(window_list, i);
        CFNumberRef owner_ref = CFDictionaryGetValue(window, kCGWindowOwnerPID);
        CFNumberRef layer_ref = CFDictionaryGetValue(window, kCGWindowLayer);
        pid_t owner = 0;
        int layer = 0;
        if (!owner_ref || !layer_ref) continue;
        CFNumberGetValue(owner_ref, kCFNumberIntType, &owner);
        CFNumberGetValue(layer_ref, kCFNumberIntType, &layer);
        if (owner != pid) continue;
        shield |= layer == 19;
        spaces_bar |= layer == 14;
    }
    CFRelease(window_list);

    // The shield alone may also occur in other Expose modes. Require the
    // Spaces bar as well, and tolerate brief surface gaps during animations.
    if (shield && spaces_bar) {
        g_mission_control_surface_misses = 0;
        if (!g_mission_control_surface_visible) {
            g_mission_control_surface_visible = true;
            event_loop_post(&g_event_loop, MISSION_CONTROL_SHOW_ALL_WINDOWS, NULL, 0);
        }
    } else if (g_mission_control_surface_visible && ++g_mission_control_surface_misses >= 3) {
        g_mission_control_surface_visible = false;
        event_loop_post(&g_event_loop, MISSION_CONTROL_EXIT, NULL, 0);
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static OBSERVER_CALLBACK(mission_control_notification_handler)
{
    if (CFEqual(notification, kAXExposeShowAllWindows)) {
        event_loop_post(&g_event_loop, MISSION_CONTROL_SHOW_ALL_WINDOWS, NULL, 0);
    } else if (CFEqual(notification, kAXExposeShowFrontWindows)) {
        event_loop_post(&g_event_loop, MISSION_CONTROL_SHOW_FRONT_WINDOWS, NULL, 0);
    } else if (CFEqual(notification, kAXExposeShowDesktop)) {
        event_loop_post(&g_event_loop, MISSION_CONTROL_SHOW_DESKTOP, NULL, 0);
    } else if (CFEqual(notification, kAXExposeExit)) {
        event_loop_post(&g_event_loop, MISSION_CONTROL_EXIT, NULL, 0);
    }
}
#pragma clang diagnostic pop

void mission_control_observe(void)
{
    if (workspace_is_macos_goldengate()) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (g_mission_control_timer) return;
            g_mission_control_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
            dispatch_source_set_timer(g_mission_control_timer, DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
            dispatch_source_set_event_handler(g_mission_control_timer, ^{
                @autoreleasepool { mission_control_poll(); }
            });
            dispatch_resume(g_mission_control_timer);
        });
        return;
    }

    if (!g_mission_control_observer.is_observing) {
        uint32_t pid = workspace_get_dock_pid();
        g_mission_control_observer.ref = AXUIElementCreateApplication(pid);

        if (pid && g_mission_control_observer.ref) {
            if (AXObserverCreate(pid, mission_control_notification_handler, &g_mission_control_observer.observer_ref) == kAXErrorSuccess) {
                AXObserverAddNotification(g_mission_control_observer.observer_ref, g_mission_control_observer.ref, kAXExposeShowAllWindows, NULL);
                AXObserverAddNotification(g_mission_control_observer.observer_ref, g_mission_control_observer.ref, kAXExposeShowFrontWindows, NULL);
                AXObserverAddNotification(g_mission_control_observer.observer_ref, g_mission_control_observer.ref, kAXExposeShowDesktop, NULL);
                AXObserverAddNotification(g_mission_control_observer.observer_ref, g_mission_control_observer.ref, kAXExposeExit, NULL);

                g_mission_control_observer.is_observing = true;
                CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(g_mission_control_observer.observer_ref), kCFRunLoopDefaultMode);
            }
        }
    }
}

void mission_control_unobserve(void)
{
    // Called when Dock restarts. The WindowManager watcher is independent
    // of Dock and must keep its current session and single timer alive.
    if (workspace_is_macos_goldengate()) return;

    if (g_mission_control_observer.is_observing) {
        AXObserverRemoveNotification(g_mission_control_observer.observer_ref, g_mission_control_observer.ref, kAXExposeShowAllWindows);
        AXObserverRemoveNotification(g_mission_control_observer.observer_ref, g_mission_control_observer.ref, kAXExposeShowFrontWindows);
        AXObserverRemoveNotification(g_mission_control_observer.observer_ref, g_mission_control_observer.ref, kAXExposeShowDesktop);
        AXObserverRemoveNotification(g_mission_control_observer.observer_ref, g_mission_control_observer.ref, kAXExposeExit);

        g_mission_control_observer.is_observing = false;
        CFRunLoopSourceInvalidate(AXObserverGetRunLoopSource(g_mission_control_observer.observer_ref));
        CFRelease(g_mission_control_observer.observer_ref);
        CFRelease(g_mission_control_observer.ref);
    }
}

static inline bool mission_control_is_active(void)
{
    return g_mission_control_mode != MISSION_CONTROL_MODE_INACTIVE;
}
