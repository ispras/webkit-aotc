/*
 * Copyright (C) 2010, 2011, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "TestInvocation.h"

#include "PlatformWebView.h"
#include "StringFunctions.h"
#include "TestController.h"
#include <climits>
#include <cstdio>
#include <WebKit2/WKContextPrivate.h>
#include <WebKit2/WKData.h>
#include <WebKit2/WKDictionary.h>
#include <WebKit2/WKInspector.h>
#include <WebKit2/WKRetainPtr.h>
#include <wtf/PassOwnPtr.h>
#include <wtf/StdLibExtras.h>
#include <wtf/text/CString.h>

#if PLATFORM(MAC) && !PLATFORM(IOS)
#include <Carbon/Carbon.h>
#endif

#if PLATFORM(COCOA)
#include <WebKit2/WKPagePrivateMac.h>
#endif

#include <unistd.h> // For getcwd.

using namespace WebKit;
using namespace std;

namespace WTR {

static WKURLRef createWKURL(const char* pathOrURL)
{
    if (strstr(pathOrURL, "http://") || strstr(pathOrURL, "https://") || strstr(pathOrURL, "file://"))
        return WKURLCreateWithUTF8CString(pathOrURL);

    // Creating from filesytem path.
    size_t length = strlen(pathOrURL);
    if (!length)
        return 0;

    const char separator = '/';
    bool isAbsolutePath = pathOrURL[0] == separator;
    const char* filePrefix = "file://";
    static const size_t prefixLength = strlen(filePrefix);

    std::unique_ptr<char[]> buffer;
    if (isAbsolutePath) {
        buffer = std::make_unique<char[]>(prefixLength + length + 1);
        strcpy(buffer.get(), filePrefix);
        strcpy(buffer.get() + prefixLength, pathOrURL);
    } else {
        buffer = std::make_unique<char[]>(prefixLength + PATH_MAX + length + 2); // 1 for the separator
        strcpy(buffer.get(), filePrefix);
        if (!getcwd(buffer.get() + prefixLength, PATH_MAX))
            return 0;
        size_t numCharacters = strlen(buffer.get());
        buffer[numCharacters] = separator;
        strcpy(buffer.get() + numCharacters + 1, pathOrURL);
    }

    return WKURLCreateWithUTF8CString(buffer.get());
}

TestInvocation::TestInvocation(const std::string& pathOrURL)
    : m_url(AdoptWK, createWKURL(pathOrURL.c_str()))
    , m_pathOrURL(pathOrURL)
    , m_dumpPixels(false)
    , m_timeout(0)
    , m_gotInitialResponse(false)
    , m_gotFinalMessage(false)
    , m_gotRepaint(false)
    , m_error(false)
    , m_webProcessIsUnresponsive(false)
{
}

TestInvocation::~TestInvocation()
{
}

void TestInvocation::setIsPixelTest(const std::string& expectedPixelHash)
{
    m_dumpPixels = true;
    m_expectedPixelHash = expectedPixelHash;
}

void TestInvocation::setCustomTimeout(int timeout)
{
    m_timeout = timeout;
}

static void sizeWebViewForCurrentTest(const char* pathOrURL)
{
    bool isSVGW3CTest = strstr(pathOrURL, "svg/W3C-SVG-1.1") || strstr(pathOrURL, "svg\\W3C-SVG-1.1");

    if (isSVGW3CTest)
        TestController::shared().mainWebView()->resizeTo(TestController::w3cSVGViewWidth, TestController::w3cSVGViewHeight);
    else
        TestController::shared().mainWebView()->resizeTo(TestController::viewWidth, TestController::viewHeight);
}

static void changeWindowScaleIfNeeded(const char* pathOrURL)
{
    WTF::String localPathOrUrl = String(pathOrURL);
    bool needsHighDPIWindow = localPathOrUrl.findIgnoringCase("hidpi-") != notFound;
    TestController::shared().mainWebView()->changeWindowScaleIfNeeded(needsHighDPIWindow ? 2 : 1);
}

static bool shouldLogFrameLoadDelegates(const char* pathOrURL)
{
    return strstr(pathOrURL, "loading/");
}

#if PLATFORM(COCOA)
static bool shouldUseThreadedScrolling(const char* pathOrURL)
{
    return strstr(pathOrURL, "tiled-drawing/") || strstr(pathOrURL, "tiled-drawing\\");
}
#endif

static bool shouldLogHistoryClientCallbacks(const char* pathOrURL)
{
    return strstr(pathOrURL, "globalhistory/");
}

static void updateThreadedScrollingForCurrentTest(const char* pathOrURL)
{
#if PLATFORM(COCOA)
    WKRetainPtr<WKMutableDictionaryRef> viewOptions = adoptWK(WKMutableDictionaryCreate());
    WKRetainPtr<WKStringRef> useThreadedScrollingKey = adoptWK(WKStringCreateWithUTF8CString("ThreadedScrolling"));
    WKRetainPtr<WKBooleanRef> useThreadedScrollingValue = adoptWK(WKBooleanCreate(shouldUseThreadedScrolling(pathOrURL)));
    WKDictionarySetItem(viewOptions.get(), useThreadedScrollingKey.get(), useThreadedScrollingValue.get());

    WKRetainPtr<WKStringRef> useRemoteLayerTreeKey = adoptWK(WKStringCreateWithUTF8CString("RemoteLayerTree"));
    WKRetainPtr<WKBooleanRef> useRemoteLayerTreeValue = adoptWK(WKBooleanCreate(TestController::shared().shouldUseRemoteLayerTree()));
    WKDictionarySetItem(viewOptions.get(), useRemoteLayerTreeKey.get(), useRemoteLayerTreeValue.get());

    TestController::shared().ensureViewSupportsOptions(viewOptions.get());
#else
    UNUSED_PARAM(pathOrURL);
#endif
}

static bool shouldUseFixedLayout(const char* pathOrURL)
{
#if ENABLE(CSS_DEVICE_ADAPTATION)
    if (strstr(pathOrURL, "device-adapt/") || strstr(pathOrURL, "device-adapt\\"))
        return true;
#endif

#if USE(TILED_BACKING_STORE) && PLATFORM(EFL)
    if (strstr(pathOrURL, "sticky/") || strstr(pathOrURL, "sticky\\"))
        return true;
#endif
    return false;

    UNUSED_PARAM(pathOrURL);
}

static void updateLayoutType(const char* pathOrURL)
{
    WKRetainPtr<WKMutableDictionaryRef> viewOptions = adoptWK(WKMutableDictionaryCreate());
    WKRetainPtr<WKStringRef> useFixedLayoutKey = adoptWK(WKStringCreateWithUTF8CString("UseFixedLayout"));
    WKRetainPtr<WKBooleanRef> useFixedLayoutValue = adoptWK(WKBooleanCreate(shouldUseFixedLayout(pathOrURL)));
    WKDictionarySetItem(viewOptions.get(), useFixedLayoutKey.get(), useFixedLayoutValue.get());

    TestController::shared().ensureViewSupportsOptions(viewOptions.get());
}

void TestInvocation::invoke()
{
    TestController::TimeoutDuration timeoutToUse = TestController::LongTimeout;
    changeWindowScaleIfNeeded(m_pathOrURL.c_str());
    sizeWebViewForCurrentTest(m_pathOrURL.c_str());
    updateLayoutType(m_pathOrURL.c_str());
    updateThreadedScrollingForCurrentTest(m_pathOrURL.c_str());

    m_textOutput.clear();

    TestController::shared().setShouldLogHistoryClientCallbacks(shouldLogHistoryClientCallbacks(m_pathOrURL.c_str()));

    WKRetainPtr<WKStringRef> messageName = adoptWK(WKStringCreateWithUTF8CString("BeginTest"));
    WKRetainPtr<WKMutableDictionaryRef> beginTestMessageBody = adoptWK(WKMutableDictionaryCreate());

    WKRetainPtr<WKStringRef> dumpFrameLoadDelegatesKey = adoptWK(WKStringCreateWithUTF8CString("DumpFrameLoadDelegates"));
    WKRetainPtr<WKBooleanRef> dumpFrameLoadDelegatesValue = adoptWK(WKBooleanCreate(shouldLogFrameLoadDelegates(m_pathOrURL.c_str())));
    WKDictionarySetItem(beginTestMessageBody.get(), dumpFrameLoadDelegatesKey.get(), dumpFrameLoadDelegatesValue.get());

    WKRetainPtr<WKStringRef> dumpPixelsKey = adoptWK(WKStringCreateWithUTF8CString("DumpPixels"));
    WKRetainPtr<WKBooleanRef> dumpPixelsValue = adoptWK(WKBooleanCreate(m_dumpPixels));
    WKDictionarySetItem(beginTestMessageBody.get(), dumpPixelsKey.get(), dumpPixelsValue.get());

    WKRetainPtr<WKStringRef> useWaitToDumpWatchdogTimerKey = adoptWK(WKStringCreateWithUTF8CString("UseWaitToDumpWatchdogTimer"));
    WKRetainPtr<WKBooleanRef> useWaitToDumpWatchdogTimerValue = adoptWK(WKBooleanCreate(TestController::shared().useWaitToDumpWatchdogTimer()));
    WKDictionarySetItem(beginTestMessageBody.get(), useWaitToDumpWatchdogTimerKey.get(), useWaitToDumpWatchdogTimerValue.get());

    WKRetainPtr<WKStringRef> timeoutKey = adoptWK(WKStringCreateWithUTF8CString("Timeout"));
    WKRetainPtr<WKUInt64Ref> timeoutValue = adoptWK(WKUInt64Create(m_timeout));
    WKDictionarySetItem(beginTestMessageBody.get(), timeoutKey.get(), timeoutValue.get());

    WKContextPostMessageToInjectedBundle(TestController::shared().context(), messageName.get(), beginTestMessageBody.get());

    TestController::shared().runUntil(m_gotInitialResponse, TestController::ShortTimeout);
    if (!m_gotInitialResponse) {
        m_errorMessage = "Timed out waiting for initial response from web process\n";
        m_webProcessIsUnresponsive = true;
        goto end;
    }
    if (m_error)
        goto end;

    WKPageLoadURL(TestController::shared().mainWebView()->page(), m_url.get());

    if (TestController::shared().useWaitToDumpWatchdogTimer()) {
        if (m_timeout > 0)
            timeoutToUse = TestController::CustomTimeout;
    } else
        timeoutToUse = TestController::NoTimeout;
    TestController::shared().runUntil(m_gotFinalMessage, timeoutToUse);

    if (!m_gotFinalMessage) {
        m_errorMessage = "Timed out waiting for final message from web process\n";
        m_webProcessIsUnresponsive = true;
        goto end;
    }
    if (m_error)
        goto end;

    dumpResults();

end:
#if ENABLE(INSPECTOR) && !PLATFORM(IOS)
    if (m_gotInitialResponse)
        WKInspectorClose(WKPageGetInspector(TestController::shared().mainWebView()->page()));
#endif // ENABLE(INSPECTOR)

    if (m_webProcessIsUnresponsive)
        dumpWebProcessUnresponsiveness();
    else if (!TestController::shared().resetStateToConsistentValues()) {
        m_errorMessage = "Timed out loading about:blank before the next test";
        dumpWebProcessUnresponsiveness();
    }
}

void TestInvocation::dumpWebProcessUnresponsiveness()
{
    dumpWebProcessUnresponsiveness(m_errorMessage.c_str());
}

void TestInvocation::dumpWebProcessUnresponsiveness(const char* errorMessage)
{
    const char* errorMessageToStderr = 0;
#if PLATFORM(COCOA)
    char buffer[64];
    pid_t pid = WKPageGetProcessIdentifier(TestController::shared().mainWebView()->page());
    sprintf(buffer, "#PROCESS UNRESPONSIVE - WebProcess (pid %ld)\n", static_cast<long>(pid));
    errorMessageToStderr = buffer;
#else
    errorMessageToStderr = "#PROCESS UNRESPONSIVE - WebProcess";
#endif

    dump(errorMessage, errorMessageToStderr, true);
}

void TestInvocation::dump(const char* textToStdout, const char* textToStderr, bool seenError)
{
    printf("Content-Type: text/plain\n");
    if (textToStdout)
        fputs(textToStdout, stdout);
    if (textToStderr)
        fputs(textToStderr, stderr);

    fputs("#EOF\n", stdout);
    fputs("#EOF\n", stderr);
    if (seenError)
        fputs("#EOF\n", stdout);
    fflush(stdout);
    fflush(stderr);
}

void TestInvocation::forceRepaintDoneCallback(WKErrorRef, void* context)
{
    TestInvocation* testInvocation = static_cast<TestInvocation*>(context);
    testInvocation->m_gotRepaint = true;
    TestController::shared().notifyDone();
}

void TestInvocation::dumpResults()
{
    if (m_textOutput.length() || !m_audioResult)
        dump(m_textOutput.toString().utf8().data());
    else
        dumpAudio(m_audioResult.get());

    if (m_dumpPixels && m_pixelResult) {
        if (PlatformWebView::windowSnapshotEnabled()) {
            m_gotRepaint = false;
            WKPageForceRepaint(TestController::shared().mainWebView()->page(), this, TestInvocation::forceRepaintDoneCallback);
            TestController::shared().runUntil(m_gotRepaint, TestController::ShortTimeout);
            if (!m_gotRepaint) {
                m_errorMessage = "Timed out waiting for pre-pixel dump repaint\n";
                m_webProcessIsUnresponsive = true;
                return;
            }
        }
        dumpPixelsAndCompareWithExpected(m_pixelResult.get(), m_repaintRects.get());
    }

    fputs("#EOF\n", stdout);
    fflush(stdout);
    fflush(stderr);
}

void TestInvocation::dumpAudio(WKDataRef audioData)
{
    size_t length = WKDataGetSize(audioData);
    if (!length)
        return;

    const unsigned char* data = WKDataGetBytes(audioData);

    printf("Content-Type: audio/wav\n");
    printf("Content-Length: %lu\n", static_cast<unsigned long>(length));

    fwrite(data, 1, length, stdout);
    printf("#EOF\n");
    fprintf(stderr, "#EOF\n");
}

bool TestInvocation::compareActualHashToExpectedAndDumpResults(const char actualHash[33])
{
    // Compute the hash of the bitmap context pixels
    fprintf(stdout, "\nActualHash: %s\n", actualHash);

    if (!m_expectedPixelHash.length())
        return false;

    ASSERT(m_expectedPixelHash.length() == 32);
    fprintf(stdout, "\nExpectedHash: %s\n", m_expectedPixelHash.c_str());

    // FIXME: Do case insensitive compare.
    return m_expectedPixelHash == actualHash;
}

void TestInvocation::didReceiveMessageFromInjectedBundle(WKStringRef messageName, WKTypeRef messageBody)
{
    if (WKStringIsEqualToUTF8CString(messageName, "Error")) {
        // Set all states to true to stop spinning the runloop.
        m_gotInitialResponse = true;
        m_gotFinalMessage = true;
        m_error = true;
        m_errorMessage = "FAIL\n";
        TestController::shared().notifyDone();
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "Ack")) {
        ASSERT(WKGetTypeID(messageBody) == WKStringGetTypeID());
        WKStringRef messageBodyString = static_cast<WKStringRef>(messageBody);
        if (WKStringIsEqualToUTF8CString(messageBodyString, "BeginTest")) {
            m_gotInitialResponse = true;
            TestController::shared().notifyDone();
            return;
        }

        ASSERT_NOT_REACHED();
    }

    if (WKStringIsEqualToUTF8CString(messageName, "Done")) {
        ASSERT(WKGetTypeID(messageBody) == WKDictionaryGetTypeID());
        WKDictionaryRef messageBodyDictionary = static_cast<WKDictionaryRef>(messageBody);

        WKRetainPtr<WKStringRef> pixelResultKey = adoptWK(WKStringCreateWithUTF8CString("PixelResult"));
        m_pixelResult = static_cast<WKImageRef>(WKDictionaryGetItemForKey(messageBodyDictionary, pixelResultKey.get()));
        ASSERT(!m_pixelResult || m_dumpPixels);

        WKRetainPtr<WKStringRef> repaintRectsKey = adoptWK(WKStringCreateWithUTF8CString("RepaintRects"));
        m_repaintRects = static_cast<WKArrayRef>(WKDictionaryGetItemForKey(messageBodyDictionary, repaintRectsKey.get()));

        WKRetainPtr<WKStringRef> audioResultKey =  adoptWK(WKStringCreateWithUTF8CString("AudioResult"));
        m_audioResult = static_cast<WKDataRef>(WKDictionaryGetItemForKey(messageBodyDictionary, audioResultKey.get()));

        m_gotFinalMessage = true;
        TestController::shared().notifyDone();
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "TextOutput")) {
        ASSERT(WKGetTypeID(messageBody) == WKStringGetTypeID());
        WKStringRef textOutput = static_cast<WKStringRef>(messageBody);
        m_textOutput.append(toWTFString(textOutput));
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "BeforeUnloadReturnValue")) {
        ASSERT(WKGetTypeID(messageBody) == WKBooleanGetTypeID());
        WKBooleanRef beforeUnloadReturnValue = static_cast<WKBooleanRef>(messageBody);
        TestController::shared().setBeforeUnloadReturnValue(WKBooleanGetValue(beforeUnloadReturnValue));
        return;
    }
    
    if (WKStringIsEqualToUTF8CString(messageName, "AddChromeInputField")) {
        TestController::shared().mainWebView()->addChromeInputField();
        WKRetainPtr<WKStringRef> messageName = adoptWK(WKStringCreateWithUTF8CString("CallAddChromeInputFieldCallback"));
        WKContextPostMessageToInjectedBundle(TestController::shared().context(), messageName.get(), 0);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "RemoveChromeInputField")) {
        TestController::shared().mainWebView()->removeChromeInputField();
        WKRetainPtr<WKStringRef> messageName = adoptWK(WKStringCreateWithUTF8CString("CallRemoveChromeInputFieldCallback"));
        WKContextPostMessageToInjectedBundle(TestController::shared().context(), messageName.get(), 0);
        return;
    }
    
    if (WKStringIsEqualToUTF8CString(messageName, "FocusWebView")) {
        TestController::shared().mainWebView()->makeWebViewFirstResponder();
        WKRetainPtr<WKStringRef> messageName = adoptWK(WKStringCreateWithUTF8CString("CallFocusWebViewCallback"));
        WKContextPostMessageToInjectedBundle(TestController::shared().context(), messageName.get(), 0);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetBackingScaleFactor")) {
        ASSERT(WKGetTypeID(messageBody) == WKDoubleGetTypeID());
        double backingScaleFactor = WKDoubleGetValue(static_cast<WKDoubleRef>(messageBody));
        WKPageSetCustomBackingScaleFactor(TestController::shared().mainWebView()->page(), backingScaleFactor);

        WKRetainPtr<WKStringRef> messageName = adoptWK(WKStringCreateWithUTF8CString("CallSetBackingScaleFactorCallback"));
        WKContextPostMessageToInjectedBundle(TestController::shared().context(), messageName.get(), 0);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SimulateWebNotificationClick")) {
        ASSERT(WKGetTypeID(messageBody) == WKUInt64GetTypeID());
        uint64_t notificationID = WKUInt64GetValue(static_cast<WKUInt64Ref>(messageBody));
        TestController::shared().simulateWebNotificationClick(notificationID);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetGeolocationPermission")) {
        ASSERT(WKGetTypeID(messageBody) == WKBooleanGetTypeID());
        WKBooleanRef enabledWK = static_cast<WKBooleanRef>(messageBody);
        TestController::shared().setGeolocationPermission(WKBooleanGetValue(enabledWK));
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetMockGeolocationPosition")) {
        ASSERT(WKGetTypeID(messageBody) == WKDictionaryGetTypeID());
        WKDictionaryRef messageBodyDictionary = static_cast<WKDictionaryRef>(messageBody);

        WKRetainPtr<WKStringRef> latitudeKeyWK(AdoptWK, WKStringCreateWithUTF8CString("latitude"));
        WKDoubleRef latitudeWK = static_cast<WKDoubleRef>(WKDictionaryGetItemForKey(messageBodyDictionary, latitudeKeyWK.get()));
        double latitude = WKDoubleGetValue(latitudeWK);

        WKRetainPtr<WKStringRef> longitudeKeyWK(AdoptWK, WKStringCreateWithUTF8CString("longitude"));
        WKDoubleRef longitudeWK = static_cast<WKDoubleRef>(WKDictionaryGetItemForKey(messageBodyDictionary, longitudeKeyWK.get()));
        double longitude = WKDoubleGetValue(longitudeWK);

        WKRetainPtr<WKStringRef> accuracyKeyWK(AdoptWK, WKStringCreateWithUTF8CString("accuracy"));
        WKDoubleRef accuracyWK = static_cast<WKDoubleRef>(WKDictionaryGetItemForKey(messageBodyDictionary, accuracyKeyWK.get()));
        double accuracy = WKDoubleGetValue(accuracyWK);

        WKRetainPtr<WKStringRef> providesAltitudeKeyWK(AdoptWK, WKStringCreateWithUTF8CString("providesAltitude"));
        WKBooleanRef providesAltitudeWK = static_cast<WKBooleanRef>(WKDictionaryGetItemForKey(messageBodyDictionary, providesAltitudeKeyWK.get()));
        bool providesAltitude = WKBooleanGetValue(providesAltitudeWK);

        WKRetainPtr<WKStringRef> altitudeKeyWK(AdoptWK, WKStringCreateWithUTF8CString("altitude"));
        WKDoubleRef altitudeWK = static_cast<WKDoubleRef>(WKDictionaryGetItemForKey(messageBodyDictionary, altitudeKeyWK.get()));
        double altitude = WKDoubleGetValue(altitudeWK);

        WKRetainPtr<WKStringRef> providesAltitudeAccuracyKeyWK(AdoptWK, WKStringCreateWithUTF8CString("providesAltitudeAccuracy"));
        WKBooleanRef providesAltitudeAccuracyWK = static_cast<WKBooleanRef>(WKDictionaryGetItemForKey(messageBodyDictionary, providesAltitudeAccuracyKeyWK.get()));
        bool providesAltitudeAccuracy = WKBooleanGetValue(providesAltitudeAccuracyWK);

        WKRetainPtr<WKStringRef> altitudeAccuracyKeyWK(AdoptWK, WKStringCreateWithUTF8CString("altitudeAccuracy"));
        WKDoubleRef altitudeAccuracyWK = static_cast<WKDoubleRef>(WKDictionaryGetItemForKey(messageBodyDictionary, altitudeAccuracyKeyWK.get()));
        double altitudeAccuracy = WKDoubleGetValue(altitudeAccuracyWK);

        WKRetainPtr<WKStringRef> providesHeadingKeyWK(AdoptWK, WKStringCreateWithUTF8CString("providesHeading"));
        WKBooleanRef providesHeadingWK = static_cast<WKBooleanRef>(WKDictionaryGetItemForKey(messageBodyDictionary, providesHeadingKeyWK.get()));
        bool providesHeading = WKBooleanGetValue(providesHeadingWK);

        WKRetainPtr<WKStringRef> headingKeyWK(AdoptWK, WKStringCreateWithUTF8CString("heading"));
        WKDoubleRef headingWK = static_cast<WKDoubleRef>(WKDictionaryGetItemForKey(messageBodyDictionary, headingKeyWK.get()));
        double heading = WKDoubleGetValue(headingWK);

        WKRetainPtr<WKStringRef> providesSpeedKeyWK(AdoptWK, WKStringCreateWithUTF8CString("providesSpeed"));
        WKBooleanRef providesSpeedWK = static_cast<WKBooleanRef>(WKDictionaryGetItemForKey(messageBodyDictionary, providesSpeedKeyWK.get()));
        bool providesSpeed = WKBooleanGetValue(providesSpeedWK);

        WKRetainPtr<WKStringRef> speedKeyWK(AdoptWK, WKStringCreateWithUTF8CString("speed"));
        WKDoubleRef speedWK = static_cast<WKDoubleRef>(WKDictionaryGetItemForKey(messageBodyDictionary, speedKeyWK.get()));
        double speed = WKDoubleGetValue(speedWK);

        TestController::shared().setMockGeolocationPosition(latitude, longitude, accuracy, providesAltitude, altitude, providesAltitudeAccuracy, altitudeAccuracy, providesHeading, heading, providesSpeed, speed);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetMockGeolocationPositionUnavailableError")) {
        ASSERT(WKGetTypeID(messageBody) == WKStringGetTypeID());
        WKStringRef errorMessage = static_cast<WKStringRef>(messageBody);
        TestController::shared().setMockGeolocationPositionUnavailableError(errorMessage);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetCustomPolicyDelegate")) {
        ASSERT(WKGetTypeID(messageBody) == WKDictionaryGetTypeID());
        WKDictionaryRef messageBodyDictionary = static_cast<WKDictionaryRef>(messageBody);

        WKRetainPtr<WKStringRef> enabledKeyWK(AdoptWK, WKStringCreateWithUTF8CString("enabled"));
        WKBooleanRef enabledWK = static_cast<WKBooleanRef>(WKDictionaryGetItemForKey(messageBodyDictionary, enabledKeyWK.get()));
        bool enabled = WKBooleanGetValue(enabledWK);

        WKRetainPtr<WKStringRef> permissiveKeyWK(AdoptWK, WKStringCreateWithUTF8CString("permissive"));
        WKBooleanRef permissiveWK = static_cast<WKBooleanRef>(WKDictionaryGetItemForKey(messageBodyDictionary, permissiveKeyWK.get()));
        bool permissive = WKBooleanGetValue(permissiveWK);

        TestController::shared().setCustomPolicyDelegate(enabled, permissive);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetHidden")) {
        ASSERT(WKGetTypeID(messageBody) == WKDictionaryGetTypeID());
        WKDictionaryRef messageBodyDictionary = static_cast<WKDictionaryRef>(messageBody);

        WKRetainPtr<WKStringRef> isInitialKeyWK(AdoptWK, WKStringCreateWithUTF8CString("hidden"));
        WKBooleanRef hiddenWK = static_cast<WKBooleanRef>(WKDictionaryGetItemForKey(messageBodyDictionary, isInitialKeyWK.get()));
        bool hidden = WKBooleanGetValue(hiddenWK);

        TestController::shared().setHidden(hidden);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "ProcessWorkQueue")) {
        if (TestController::shared().workQueueManager().processWorkQueue()) {
            WKRetainPtr<WKStringRef> messageName = adoptWK(WKStringCreateWithUTF8CString("WorkQueueProcessedCallback"));
            WKContextPostMessageToInjectedBundle(TestController::shared().context(), messageName.get(), 0);
        }
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "QueueBackNavigation")) {
        ASSERT(WKGetTypeID(messageBody) == WKUInt64GetTypeID());
        uint64_t stepCount = WKUInt64GetValue(static_cast<WKUInt64Ref>(messageBody));
        TestController::shared().workQueueManager().queueBackNavigation(stepCount);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "QueueForwardNavigation")) {
        ASSERT(WKGetTypeID(messageBody) == WKUInt64GetTypeID());
        uint64_t stepCount = WKUInt64GetValue(static_cast<WKUInt64Ref>(messageBody));
        TestController::shared().workQueueManager().queueForwardNavigation(stepCount);
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "QueueLoad")) {
        ASSERT(WKGetTypeID(messageBody) == WKDictionaryGetTypeID());
        WKDictionaryRef loadDataDictionary = static_cast<WKDictionaryRef>(messageBody);

        WKRetainPtr<WKStringRef> urlKey(AdoptWK, WKStringCreateWithUTF8CString("url"));
        WKStringRef urlWK = static_cast<WKStringRef>(WKDictionaryGetItemForKey(loadDataDictionary, urlKey.get()));

        WKRetainPtr<WKStringRef> targetKey(AdoptWK, WKStringCreateWithUTF8CString("target"));
        WKStringRef targetWK = static_cast<WKStringRef>(WKDictionaryGetItemForKey(loadDataDictionary, targetKey.get()));

        TestController::shared().workQueueManager().queueLoad(toWTFString(urlWK), toWTFString(targetWK));
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "QueueLoadHTMLString")) {
        ASSERT(WKGetTypeID(messageBody) == WKDictionaryGetTypeID());
        WKDictionaryRef loadDataDictionary = static_cast<WKDictionaryRef>(messageBody);

        WKRetainPtr<WKStringRef> contentKey(AdoptWK, WKStringCreateWithUTF8CString("content"));
        WKStringRef contentWK = static_cast<WKStringRef>(WKDictionaryGetItemForKey(loadDataDictionary, contentKey.get()));

        WKRetainPtr<WKStringRef> baseURLKey(AdoptWK, WKStringCreateWithUTF8CString("baseURL"));
        WKStringRef baseURLWK = static_cast<WKStringRef>(WKDictionaryGetItemForKey(loadDataDictionary, baseURLKey.get()));

        WKRetainPtr<WKStringRef> unreachableURLKey(AdoptWK, WKStringCreateWithUTF8CString("unreachableURL"));
        WKStringRef unreachableURLWK = static_cast<WKStringRef>(WKDictionaryGetItemForKey(loadDataDictionary, unreachableURLKey.get()));

        TestController::shared().workQueueManager().queueLoadHTMLString(toWTFString(contentWK), baseURLWK ? toWTFString(baseURLWK) : String(), unreachableURLWK ? toWTFString(unreachableURLWK) : String());
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "QueueReload")) {
        TestController::shared().workQueueManager().queueReload();
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "QueueLoadingScript")) {
        ASSERT(WKGetTypeID(messageBody) == WKStringGetTypeID());
        WKStringRef script = static_cast<WKStringRef>(messageBody);
        TestController::shared().workQueueManager().queueLoadingScript(toWTFString(script));
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "QueueNonLoadingScript")) {
        ASSERT(WKGetTypeID(messageBody) == WKStringGetTypeID());
        WKStringRef script = static_cast<WKStringRef>(messageBody);
        TestController::shared().workQueueManager().queueNonLoadingScript(toWTFString(script));
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetHandlesAuthenticationChallenge")) {
        ASSERT(WKGetTypeID(messageBody) == WKBooleanGetTypeID());
        WKBooleanRef value = static_cast<WKBooleanRef>(messageBody);
        TestController::shared().setHandlesAuthenticationChallenges(WKBooleanGetValue(value));
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetAuthenticationUsername")) {
        ASSERT(WKGetTypeID(messageBody) == WKStringGetTypeID());
        WKStringRef username = static_cast<WKStringRef>(messageBody);
        TestController::shared().setAuthenticationUsername(toWTFString(username));
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetAuthenticationPassword")) {
        ASSERT(WKGetTypeID(messageBody) == WKStringGetTypeID());
        WKStringRef password = static_cast<WKStringRef>(messageBody);
        TestController::shared().setAuthenticationPassword(toWTFString(password));
        return;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SetBlockAllPlugins")) {
        ASSERT(WKGetTypeID(messageBody) == WKBooleanGetTypeID());
        WKBooleanRef shouldBlock = static_cast<WKBooleanRef>(messageBody);
        TestController::shared().setBlockAllPlugins(WKBooleanGetValue(shouldBlock));
        return;
    }

    ASSERT_NOT_REACHED();
}

WKRetainPtr<WKTypeRef> TestInvocation::didReceiveSynchronousMessageFromInjectedBundle(WKStringRef messageName, WKTypeRef messageBody)
{
    if (WKStringIsEqualToUTF8CString(messageName, "SetWindowIsKey")) {
        ASSERT(WKGetTypeID(messageBody) == WKBooleanGetTypeID());
        WKBooleanRef isKeyValue = static_cast<WKBooleanRef>(messageBody);
        TestController::shared().mainWebView()->setWindowIsKey(WKBooleanGetValue(isKeyValue));
        return 0;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "IsWorkQueueEmpty")) {
        bool isEmpty = TestController::shared().workQueueManager().isWorkQueueEmpty();
        WKRetainPtr<WKTypeRef> result(AdoptWK, WKBooleanCreate(isEmpty));
        return result;
    }

    if (WKStringIsEqualToUTF8CString(messageName, "SecureEventInputIsEnabled")) {
#if PLATFORM(MAC) && !PLATFORM(IOS)
        WKRetainPtr<WKBooleanRef> result(AdoptWK, WKBooleanCreate(IsSecureEventInputEnabled()));
#else
        WKRetainPtr<WKBooleanRef> result(AdoptWK, WKBooleanCreate(false));
#endif
        return result;
    }
    ASSERT_NOT_REACHED();
    return 0;
}

void TestInvocation::outputText(const WTF::String& text)
{
    m_textOutput.append(text);
}

} // namespace WTR
