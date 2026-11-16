// Exercise the public observer lifecycle with synthetic native window lists.
// No AX subscription, real timer, window movement or daemon is used.
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <Block.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

struct event_loop { int unused; };
struct event_loop g_event_loop;
volatile uint64_t __last_cmd_tab_time;
enum { SLS_SPACE_CREATED, SLS_SPACE_DESTROYED,
       SLS_WINDOW_ORDERED, SLS_WINDOW_DESTROYED, MISSION_CONTROL_SHOW_ALL_WINDOWS,
       MISSION_CONTROL_SHOW_FRONT_WINDOWS, MISSION_CONTROL_SHOW_DESKTOP, MISSION_CONTROL_EXIT };
static pid_t window_manager_pid = 643;
static pid_t dock_pid = 674;
static NSArray *inventory;
static dispatch_block_t tick;
static int events[32], event_count;

@interface TestApplication : NSObject
+ (NSArray *)runningApplicationsWithBundleIdentifier:(NSString *)identifier;
- (pid_t)processIdentifier;
@end
@implementation TestApplication
+ (NSArray *)runningApplicationsWithBundleIdentifier:(NSString *)identifier
{
    assert([identifier isEqualToString:@"com.apple.WindowManager"]);
    return window_manager_pid ? @[[[[self alloc] init] autorelease]] : @[];
}
- (pid_t)processIdentifier { return window_manager_pid; }
@end

static void event_loop_post(struct event_loop *loop, int event, void *context, int param)
{
    assert(event_count < 32);
    events[event_count++] = event;
}
static CFArrayRef test_window_list(CGWindowListOption option, CGWindowID relative)
{
    assert(option == kCGWindowListOptionOnScreenOnly && relative == 0);
    return inventory ? (CFArrayRef)CFRetain((CFArrayRef)inventory) : NULL;
}
static dispatch_source_t test_timer(dispatch_source_type_t type, uintptr_t handle, unsigned long mask, dispatch_queue_t queue)
{
    return (dispatch_source_t)1;
}
static void test_handler(dispatch_source_t source, dispatch_block_t handler)
{
    if (tick) Block_release(tick);
    tick = Block_copy(handler);
}
static void test_async(dispatch_queue_t queue, dispatch_block_t handler) { handler(); }
static void test_set_timer(dispatch_source_t source, dispatch_time_t start, uint64_t interval, uint64_t leeway) {}
static void test_resume(dispatch_object_t source) {}
static uint32_t workspace_get_dock_pid(void) { return dock_pid; }
static AXUIElementRef test_application(pid_t pid) { return (AXUIElementRef)CFRetain(CFSTR("application")); }
static AXError test_observer(pid_t pid, AXObserverCallback callback, AXObserverRef *observer)
{
    *observer = (AXObserverRef)CFRetain(CFSTR("observer"));
    return kAXErrorSuccess;
}
static AXError test_notification(AXObserverRef observer, AXUIElementRef element, CFStringRef name, void *context) { return kAXErrorSuccess; }
static AXError test_remove_notification(AXObserverRef observer, AXUIElementRef element, CFStringRef name) { return kAXErrorSuccess; }
static CFRunLoopSourceRef test_source(AXObserverRef observer) { return (CFRunLoopSourceRef)1; }
static void test_add_source(CFRunLoopRef loop, CFRunLoopSourceRef source, CFStringRef mode) {}
static void test_invalidate(CFRunLoopSourceRef source) {}
static uint64_t read_os_timer(void) { return 0; }
#define NSRunningApplication TestApplication
#define CGWindowListCopyWindowInfo test_window_list
#define dispatch_source_create test_timer
#define dispatch_source_set_event_handler test_handler
#define dispatch_source_set_timer test_set_timer
#define dispatch_resume test_resume
#define dispatch_async test_async
#define AXUIElementCreateApplication test_application
#define AXObserverCreate test_observer
#define AXObserverAddNotification test_notification
#define AXObserverRemoveNotification test_remove_notification
#define AXObserverGetRunLoopSource test_source
#define CFRunLoopAddSource test_add_source
#define CFRunLoopSourceInvalidate test_invalidate
#define OBSERVER_CALLBACK(name) void name(AXObserverRef observer, AXUIElementRef element, CFStringRef notification, void *context)
#define CONNECTION_CALLBACK(name) void name(int type, void *data, void *context)
#include "../../src/mission_control.c"

static NSDictionary *window(pid_t pid, int layer)
{
    return @{(id)kCGWindowOwnerPID: @(pid), (id)kCGWindowLayer: @(layer)};
}
static void sample(NSArray *windows, int count)
{
    inventory = windows;
    for (int i = 0; i < count; ++i) if (tick) tick();
}
int main(void)
{
    @autoreleasepool {
        mission_control_observe();
        assert(event_count == 0);

        // Wrong owners, incomplete pairs and named Dock windows must not trigger.
        sample(@[window(900,18), window(900,19), window(900,14)], 4);
        sample(@[@{(id)kCGWindowOwnerPID: @674, (id)kCGWindowLayer: @18,
                    (id)kCGWindowName: @"unrelated"}], 4);
        sample(@[window(643,19), window(900,14)], 4);
        sample(@[window(643,14)], 4);
        sample(@[@{}, window(643,16)], 4);
        assert(event_count == 0);

        NSArray *open = @[window(643,19), window(643,14)];
        sample(open, 4);
        assert(event_count == 1 && events[0] == MISSION_CONTROL_SHOW_ALL_WINDOWS);
        mission_control_notification_handler(NULL, NULL, kAXExposeShowAllWindows, NULL);
        mission_control_notification_handler(NULL, NULL, kAXExposeExit, NULL);
        assert(event_count == 1); // overlapping AX notifications cannot duplicate/end it

        // Transient native-query failures and short gaps must not end a session.
        sample(nil, 4);
        sample(@[], 2);
        assert(event_count == 1);
        sample(open, 1);
        sample(@[], 2);
        assert(event_count == 1);

        // A Dock restart must not reset or duplicate this independent watcher.
        mission_control_unobserve();
        mission_control_observe();
        sample(open, 2);
        assert(event_count == 1);
        sample(@[], 2);
        assert(event_count == 1);
        sample(@[], 1);
        assert(event_count == 2 && events[1] == MISSION_CONTROL_EXIT);
        sample(@[], 4);
        assert(event_count == 2);

        // Reacquire WindowManager identity; stale or mixed-owner surfaces fail.
        window_manager_pid = 901;
        sample(open, 4);
        sample(@[window(643,19), window(901,14)], 4);
        assert(event_count == 2);
        sample(@[window(901,19), window(901,14), window(901,19)], 3);
        assert(event_count == 3 && events[2] == MISSION_CONTROL_SHOW_ALL_WINDOWS);
        window_manager_pid = 0;
        sample(@[window(901,19), window(901,14)], 3);
        assert(event_count == 4 && events[3] == MISSION_CONTROL_EXIT);

        // Older native signature works even without WindowManager. Both owners
        // or a runtime handoff still describe one session, not two.
        event_count = 0;
        sample(@[], 1);
        mission_control_notification_handler(NULL, NULL, kAXExposeShowAllWindows, NULL);
        assert(event_count == 0); // AX alone is not proof of the native surface
        sample(@[window(674,18)], 4);
        assert(event_count == 1 && events[0] == MISSION_CONTROL_SHOW_ALL_WINDOWS);
        window_manager_pid = 901;
        sample(@[window(674,18), window(901,19), window(901,14)], 2);
        sample(@[window(901,19), window(901,14)], 2);
        assert(event_count == 1);
        sample(@[], 3);
        mission_control_notification_handler(NULL, NULL, kAXExposeExit, NULL);
        assert(event_count == 2 && events[1] == MISSION_CONTROL_EXIT);

        // Refresh Dock identity too; a surface belonging to the old PID fails.
        dock_pid = 888;
        sample(@[window(674,18)], 4);
        assert(event_count == 2);
        sample(@[window(888,18)], 1);
        assert(event_count == 3 && events[2] == MISSION_CONTROL_SHOW_ALL_WINDOWS);
        sample(@[], 3);
        assert(event_count == 4 && events[3] == MISSION_CONTROL_EXIT);
        dock_pid = 0;
        sample(@[window(0,18), window(0,19), window(0,14)], 4);
        assert(event_count == 4);

        // AX still distinguishes App Expose and Show Desktop. While either is
        // explicitly active, surface polling must not turn it into full MC.
        event_count = 0;
        dock_pid = 888;
        mission_control_notification_handler(NULL, NULL, kAXExposeShowFrontWindows, NULL);
        mission_control_notification_handler(NULL, NULL, kAXExposeShowFrontWindows, NULL);
        sample(@[window(888,18)], 4);
        sample(@[], 4);
        assert(event_count == 1 && events[0] == MISSION_CONTROL_SHOW_FRONT_WINDOWS);
        mission_control_notification_handler(NULL, NULL, kAXExposeExit, NULL);
        mission_control_notification_handler(NULL, NULL, kAXExposeExit, NULL);
        assert(event_count == 2 && events[1] == MISSION_CONTROL_EXIT);
        mission_control_notification_handler(NULL, NULL, kAXExposeShowDesktop, NULL);
        sample(@[window(888,18)], 4);
        assert(event_count == 3 && events[2] == MISSION_CONTROL_SHOW_DESKTOP);
        // Transition directly to full MC through its AX hint and native proof.
        inventory = @[window(901,19), window(901,14)];
        mission_control_notification_handler(NULL, NULL, kAXExposeShowAllWindows, NULL);
        assert(event_count == 5 && events[3] == MISSION_CONTROL_EXIT &&
               events[4] == MISSION_CONTROL_SHOW_ALL_WINDOWS);
        mission_control_notification_handler(NULL, NULL, kAXExposeExit, NULL);
        sample(inventory, 4);
        assert(event_count == 5);
        sample(@[], 3);
        assert(event_count == 6 && events[5] == MISSION_CONTROL_EXIT);
        // A later explicit App Expose callback can correct an earlier sample.
        sample(@[window(888,18)], 1);
        mission_control_notification_handler(NULL, NULL, kAXExposeShowFrontWindows, NULL);
        sample(inventory, 4);
        assert(event_count == 9 && events[6] == MISSION_CONTROL_SHOW_ALL_WINDOWS &&
               events[7] == MISSION_CONTROL_EXIT && events[8] == MISSION_CONTROL_SHOW_FRONT_WINDOWS);
        mission_control_notification_handler(NULL, NULL, kAXExposeExit, NULL);
        assert(event_count == 10 && events[9] == MISSION_CONTROL_EXIT);
        // Native WindowManager proof must still work if an auxiliary AX exit
        // was missed and no full-Mission-Control AX hint is delivered.
        mission_control_notification_handler(NULL, NULL, kAXExposeShowFrontWindows, NULL);
        sample(@[window(901,19), window(901,14)], 1);
        assert(event_count == 13 && events[10] == MISSION_CONTROL_SHOW_FRONT_WINDOWS &&
               events[11] == MISSION_CONTROL_EXIT && events[12] == MISSION_CONTROL_SHOW_ALL_WINDOWS);
        sample(@[], 3);
        assert(event_count == 14 && events[13] == MISSION_CONTROL_EXIT);

        mission_control_unobserve();
        Block_release(tick);
        puts("PASS: runtime native signatures, owner changes, overlap, gaps, restart and AX modes");
    }
    return 0;
}
