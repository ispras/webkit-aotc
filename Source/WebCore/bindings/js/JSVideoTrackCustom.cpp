/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"

#if ENABLE(VIDEO_TRACK)

#include "JSVideoTrack.h"

#include "JSTrackCustom.h"

using namespace JSC;

namespace WebCore {

void JSVideoTrack::visitChildren(JSCell* cell, SlotVisitor& visitor)
{
    JSVideoTrack* jsVideoTrack = jsCast<JSVideoTrack*>(cell);
    ASSERT_GC_OBJECT_INHERITS(jsVideoTrack, info());
    COMPILE_ASSERT(StructureFlags & OverridesVisitChildren, OverridesVisitChildrenWithoutSettingFlag);
    ASSERT(jsVideoTrack->structure()->typeInfo().overridesVisitChildren());
    Base::visitChildren(jsVideoTrack, visitor);

    VideoTrack& videoTrack = jsVideoTrack->impl();
    visitor.addOpaqueRoot(root(&videoTrack));
}

void JSVideoTrack::setKind(ExecState* exec, JSValue value)
{
    UNUSED_PARAM(exec);
#if ENABLE(MEDIA_SOURCE)
    const String& nativeValue(value.isEmpty() ? String() : value.toString(exec)->value(exec));
    if (exec->hadException())
        return;
    impl().setKind(nativeValue);
#else
    UNUSED_PARAM(value);
    return;
#endif
}

void JSVideoTrack::setLanguage(ExecState* exec, JSValue value)
{
    UNUSED_PARAM(exec);
#if ENABLE(MEDIA_SOURCE)
    const String& nativeValue(value.isEmpty() ? String() : value.toString(exec)->value(exec));
    if (exec->hadException())
        return;
    impl().setLanguage(nativeValue);
#else
    UNUSED_PARAM(value);
    return;
#endif
}

} // namespace WebCore

#endif
