unsigned char __src_osax_payload[1];
unsigned int __src_osax_payload_len;
unsigned char __src_osax_loader[1];
unsigned int __src_osax_loader_len;

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>

static AXError test_frame_ax_copy(AXUIElementRef, CFStringRef, CFTypeRef *);
static AXError test_frame_ax_set(AXUIElementRef, CFStringRef, CFTypeRef);
CFStringRef test_frame_display(int, uint64_t);
uint32_t test_frame_display_id(CFUUIDRef);
CFUUIDRef test_frame_display_uuid(uint32_t);
uint64_t test_frame_current_space(int, CFStringRef);
#define AXUIElementCopyAttributeValue test_frame_ax_copy
#define AXUIElementSetAttributeValue test_frame_ax_set
#define SLSCopyManagedDisplayForSpace test_frame_display
#define CGDisplayGetDisplayIDFromUUID test_frame_display_id
#define CGDisplayCreateUUIDFromDisplayID test_frame_display_uuid
#define SLSManagedDisplayGetCurrentSpace test_frame_current_space
#include "../../src/manifest.m"
#undef AXUIElementCopyAttributeValue
#undef AXUIElementSetAttributeValue
#undef SLSCopyManagedDisplayForSpace
#undef CGDisplayGetDisplayIDFromUUID
#undef CGDisplayCreateUUIDFromDisplayID
#undef SLSManagedDisplayGetCurrentSpace

#define TEST_SIG(name) bool test_##name(void)
typedef TEST_SIG(function);

#define TEST_FUNC(name, code) static TEST_SIG(name) { char *test_name = #name; bool result = true; {code} return result; }
#define TEST_CHECK(r, e) if ((r) != (e)) { printf("                   \e[1;33m%s\e[m\e[1;31m#%d %s == %s\e[m \e[1;31m(%d == %d)\e[m\n", test_name, __LINE__, #r, #e, r, e); result = false; }

#include "area.c"
#include "window_frame.c"

#define TEST_ENTRY(name) { #name, test_##name },
#define TEST_LIST                                              \
    TEST_ENTRY(display_area_is_in_direction)                   \
    TEST_ENTRY(closest_display_in_direction)                    \
    TEST_ENTRY(managed_window_frame_recovery)                   \
    TEST_ENTRY(managed_window_frame_recovery_safeguards)

static struct {
    char *name;
    test_function *func;
} tests[] = {
    TEST_LIST
};

int main(int argc, char **argv)
{
    int succeeded = 0;
    int failed = 0;
    int total = array_count(tests);
    printf("\e[1;34m -- Running %d tests\e[m\n\n", total);

    uint64_t cpu_freq  = read_cpu_freq();
    uint64_t begin_tsc = read_cpu_timer();

    for (int i = 0; i < total; ++i) {
        uint64_t tsc = read_cpu_timer();
        bool result = tests[i].func();
        double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - tsc) / (double)cpu_freq;

        printf("(%0.4fms) %s \e[1;33m%s\e[m\n", ms_elapsed, result ? "\e[1;32msuccess\e[m" : " \e[1;31mfailed\e[m", tests[i].name);
        if (result) ++succeeded; else ++failed;
    }

    double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - begin_tsc) / (double)cpu_freq;
    printf("\n\e[1;34m -- Completed (%0.4fms)\e[m\n", ms_elapsed);
    printf("\t%d \e[1;32msucceeded\e[m\n", succeeded);
    printf("\t%d \e[1;31mfailed\e[m\n", failed);
    printf("\t%d \e[1;33mtotal\e[m\n", total);

    return total == succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
