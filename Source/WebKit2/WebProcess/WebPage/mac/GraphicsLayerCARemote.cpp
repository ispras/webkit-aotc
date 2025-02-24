/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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
#include "GraphicsLayerCARemote.h"
#include "PlatformCAAnimationRemote.h"
#include "PlatformCALayerRemote.h"

using namespace WebCore;

namespace WebKit {

GraphicsLayerCARemote::~GraphicsLayerCARemote()
{
}

#if ENABLE(CSS_FILTERS)
bool GraphicsLayerCARemote::filtersCanBeComposited(const FilterOperations& filters)
{
    return PlatformCALayerRemote::filtersCanBeComposited(filters);
}
#endif

PassRefPtr<PlatformCALayer> GraphicsLayerCARemote::createPlatformCALayer(PlatformCALayer::LayerType layerType, PlatformCALayerClient* owner)
{
    return PlatformCALayerRemote::create(layerType, owner, m_context);
}

PassRefPtr<PlatformCALayer> GraphicsLayerCARemote::createPlatformCALayer(PlatformLayer* platformLayer, PlatformCALayerClient* owner)
{
    return PlatformCALayerRemote::create(platformLayer, owner, m_context);
}

PassRefPtr<PlatformCAAnimation> GraphicsLayerCARemote::createPlatformCAAnimation(PlatformCAAnimation::AnimationType type, const String& keyPath)
{
    return PlatformCAAnimationRemote::create(type, keyPath);
}

bool GraphicsLayerCARemote::addAnimation(const KeyframeValueList& valueList, const FloatSize& boxSize, const Animation* anim, const String& animationName, double timeOffset)
{
#if ENABLE(CSS_FILTERS)
    if (valueList.property() == AnimatedPropertyWebkitFilter)
        return false;
#endif
    
    return GraphicsLayerCA::addAnimation(valueList, boxSize, anim, animationName, timeOffset);
}

} // namespace WebKit
