/*
 * Copyright (C) 2012, 2013 Adobe Systems Incorporated. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef DOMPath_h
#define DOMPath_h

#if ENABLE(CANVAS_PATH)

#include "CanvasPathMethods.h"
#include "SVGMatrix.h"
#include "SVGPathUtilities.h"
#include <wtf/PassRefPtr.h>
#include <wtf/RefCounted.h>

namespace WebCore {

class DOMPath : public RefCounted<DOMPath>, public CanvasPathMethods {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static PassRefPtr<DOMPath> create() { return adoptRef(new DOMPath); }
    static PassRefPtr<DOMPath> create(const Path& path) { return adoptRef(new DOMPath(path)); }
    static PassRefPtr<DOMPath> create(const DOMPath* path) { return create(path->path()); }

    static PassRefPtr<DOMPath> create(const String& pathData)
    {
        Path path;
        buildPathFromString(pathData, path);
        return create(path);
    }

    void addPath(const DOMPath* path) { addPath(path, AffineTransform()); }
    void addPath(const DOMPath* path, const AffineTransform& transform)
    {
        if (!path || !transform.isInvertible())
            return;
        m_path.addPath(path->path(), transform);
    }

    const Path& path() const { return m_path; }

private:
    DOMPath() { }
    DOMPath(const Path& path) : CanvasPathMethods(path) { }
};

}

#endif

#endif
