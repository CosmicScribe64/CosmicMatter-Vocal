#include <ApplicationServices/ApplicationServices.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Tiny, dependency-free fallback for visually auditing custom Rack widgets.
// Rack's canvas is not exposed through macOS Accessibility, so the normal
// element-based Computer Use click path cannot address its controls.
static void pauseMs(long milliseconds) {
    const struct timespec pause = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000,
    };
    nanosleep(&pause, NULL);
}

static void postMouse(CGEventType type, CGPoint point, CGMouseButton button, int clickCount,
                      CGEventFlags flags) {
    CGEventRef event = CGEventCreateMouseEvent(NULL, type, point, button);
    if (!event) exit(1);
    if (clickCount > 0) CGEventSetIntegerValueField(event, kCGMouseEventClickState, clickCount);
    CGEventSetFlags(event, flags);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

static void moveTo(CGPoint point) {
    CGWarpMouseCursorPosition(point);
    CGAssociateMouseAndMouseCursorPosition(true);
    pauseMs(30);
}

static void clickAt(CGPoint point, CGMouseButton button, int clickCount, CGEventFlags flags) {
    const CGEventType down = button == kCGMouseButtonRight ? kCGEventRightMouseDown : kCGEventLeftMouseDown;
    const CGEventType up = button == kCGMouseButtonRight ? kCGEventRightMouseUp : kCGEventLeftMouseUp;
    moveTo(point);
    postMouse(down, point, button, clickCount, flags);
    pauseMs(55);
    postMouse(up, point, button, clickCount, flags);
}

static void scrollAt(CGPoint point, int vertical, int horizontal) {
    moveTo(point);
    CGEventRef event = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitPixel, 2,
                                                     vertical, horizontal);
    if (!event) exit(1);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

static double coordinate(const char* text) {
    char* end = NULL;
    errno = 0;
    const double value = strtod(text, &end);
    if (errno || !end || *end) {
        fprintf(stderr, "invalid screen coordinate: %s\n", text);
        exit(2);
    }
    return value;
}
static int printWindows(void) {
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly,
                                                     kCGNullWindowID);
    if (!windows) return 1;
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef window = (CFDictionaryRef)CFArrayGetValueAtIndex(windows, i);
        CFStringRef owner = (CFStringRef)CFDictionaryGetValue(window, kCGWindowOwnerName);
        CFDictionaryRef bounds = (CFDictionaryRef)CFDictionaryGetValue(window, kCGWindowBounds);
        CGRect rect = CGRectZero;
        if (!owner || !bounds || !CGRectMakeWithDictionaryRepresentation(bounds, &rect)) continue;
        char ownerText[256] = {0};
        if (!CFStringGetCString(owner, ownerText, sizeof(ownerText), kCFStringEncodingUTF8)) continue;
        if (strstr(ownerText, "Rack") || strstr(ownerText, "rack")) {
            printf("%s %.0f %.0f %.0f %.0f\n", ownerText,
                   rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
        }
    }
    CFRelease(windows);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--windows") == 0) return printWindows();
    if (argc == 3) {
        clickAt(CGPointMake(coordinate(argv[1]), coordinate(argv[2])), kCGMouseButtonLeft, 1, 0);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--move") == 0) {
        moveTo(CGPointMake(coordinate(argv[2]), coordinate(argv[3])));
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--double") == 0) {
        const CGPoint point = CGPointMake(coordinate(argv[2]), coordinate(argv[3]));
        clickAt(point, kCGMouseButtonLeft, 1, 0);
        pauseMs(90);
        clickAt(point, kCGMouseButtonLeft, 2, 0);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--right") == 0) {
        clickAt(CGPointMake(coordinate(argv[2]), coordinate(argv[3])), kCGMouseButtonRight, 1, 0);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--shift") == 0) {
        clickAt(CGPointMake(coordinate(argv[2]), coordinate(argv[3])), kCGMouseButtonLeft, 1,
                kCGEventFlagMaskShift);
        return 0;
    }
    if (argc == 6 && strcmp(argv[1], "--scroll") == 0) {
        scrollAt(CGPointMake(coordinate(argv[2]), coordinate(argv[3])),
                 (int)coordinate(argv[4]), (int)coordinate(argv[5]));
        return 0;
    }
    if (argc == 6 && strcmp(argv[1], "--drag") == 0) {
        const CGPoint from = CGPointMake(coordinate(argv[2]), coordinate(argv[3]));
        const CGPoint to = CGPointMake(coordinate(argv[4]), coordinate(argv[5]));
        moveTo(from);
        postMouse(kCGEventLeftMouseDown, from, kCGMouseButtonLeft, 1, 0);
        for (int step = 1; step <= 16; ++step) {
            const double fraction = step / 16.0;
            const CGPoint point = CGPointMake(from.x + (to.x - from.x) * fraction,
                                              from.y + (to.y - from.y) * fraction);
            postMouse(kCGEventLeftMouseDragged, point, kCGMouseButtonLeft, 1, 0);
            pauseMs(18);
        }
        postMouse(kCGEventLeftMouseUp, to, kCGMouseButtonLeft, 1, 0);
        return 0;
    }
    fprintf(stderr,
            "usage: %s <x> <y> | --move <x> <y> | --double <x> <y> | "
            "--right <x> <y> | --shift <x> <y> | --scroll <x> <y> <v> <h> | "
            "--drag <x1> <y1> <x2> <y2> | --windows\n",
            argv[0]);
    return 2;
}
