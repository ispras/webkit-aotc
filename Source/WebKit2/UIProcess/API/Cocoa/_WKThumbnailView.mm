/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
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

#import "config.h"
#import "_WKThumbnailViewInternal.h"

#if WK_API_ENABLED

#if PLATFORM(MAC)

#import "ImageOptions.h"
#import "WKAPICast.h"
#import "WKView.h"
#import "WKViewInternal.h"
#import "WebPageProxy.h"

// FIXME: Make it possible to leave a snapshot of the content presented in the WKView while the thumbnail is live.
// FIXME: Don't make new speculative tiles while thumbnailed.
// FIXME: Hide scrollbars in the thumbnail.
// FIXME: We should re-use existing tiles for unparented views, if we have them (we need to know if they've been purged; if so, repaint at scaled-down size).
// FIXME: We should switch to the low-resolution scale if a view we have high-resolution tiles for repaints.

using namespace WebCore;
using namespace WebKit;

@implementation _WKThumbnailView {
    RetainPtr<WKView> _wkView;
    WebPageProxy* _webPageProxy;

    BOOL _originalMayStartMediaWhenInWindow;
    BOOL _originalSourceViewIsInWindow;

    BOOL _shouldApplyThumbnailScale;
}

@synthesize _waitingForSnapshot = _waitingForSnapshot;

- (instancetype)initWithFrame:(NSRect)frame fromWKView:(WKView *)wkView
{
    if (!(self = [super initWithFrame:frame]))
        return nil;

    _wkView = wkView;
    _webPageProxy = toImpl([_wkView pageRef]);
    _scale = 1;

    _originalMayStartMediaWhenInWindow = _webPageProxy->mayStartMediaWhenInWindow();
    _originalSourceViewIsInWindow = !![_wkView window];

    _shouldApplyThumbnailScale = !_originalSourceViewIsInWindow;

    self.usesSnapshot = YES;

    return self;
}

- (void)_viewWasUnparented
{
    if (_shouldApplyThumbnailScale)
        _webPageProxy->setThumbnailScale(1);

    [_wkView _setThumbnailView:nil];

    self.layer.contents = nil;

    _webPageProxy->setMayStartMediaWhenInWindow(_originalMayStartMediaWhenInWindow);
}

- (void)_viewWasParented
{
    if ([_wkView _thumbnailView])
        return;

    if (_shouldApplyThumbnailScale)
        _webPageProxy->setThumbnailScale(_scale);

    if (!_originalSourceViewIsInWindow)
        _webPageProxy->setMayStartMediaWhenInWindow(false);

    [self _requestSnapshotIfNeeded];
    [_wkView _setThumbnailView:self];
}

- (void)_requestSnapshotIfNeeded
{
    if (!_usesSnapshot || _waitingForSnapshot || self.layer.contents)
        return;

    _waitingForSnapshot = YES;

    RetainPtr<_WKThumbnailView> thumbnailView = self;
    IntRect snapshotRect(IntPoint(), _webPageProxy->viewSize());
    SnapshotOptions options = SnapshotOptionsRespectDrawingAreaTransform | SnapshotOptionsInViewCoordinates;
    IntSize bitmapSize = snapshotRect.size();
    bitmapSize.scale(_webPageProxy->deviceScaleFactor());
    _webPageProxy->takeSnapshot(snapshotRect, bitmapSize, options, [thumbnailView](bool, const ShareableBitmap::Handle& imageHandle) {
        RefPtr<ShareableBitmap> bitmap = ShareableBitmap::create(imageHandle, SharedMemory::ReadOnly);
        RetainPtr<CGImageRef> cgImage = bitmap->makeCGImage();
        [thumbnailView _didTakeSnapshot:cgImage.get()];
    });
}

- (void)_didTakeSnapshot:(CGImageRef)image
{
    _waitingForSnapshot = NO;
    self.layer.sublayers = @[];
    self.layer.contents = (id)image;
}

- (void)viewDidMoveToWindow
{
    if (self.window)
        [self _viewWasParented];
    else
        [self _viewWasUnparented];
}

- (void)setScale:(CGFloat)scale
{
    if (_scale == scale)
        return;

    _scale = scale;

    if (self.window && _shouldApplyThumbnailScale)
        _webPageProxy->setThumbnailScale(_scale);
}

- (void)setUsesSnapshot:(BOOL)usesSnapshot
{
    if (_usesSnapshot == usesSnapshot)
        return;

    _usesSnapshot = usesSnapshot;

    if (!self.window)
        return;

    if (usesSnapshot)
        [self _requestSnapshotIfNeeded];
    else
        [_wkView _reparentLayerTreeInThumbnailView];
}

- (void)_setThumbnailLayer:(CALayer *)layer
{
    self.layer.sublayers = layer ? @[ layer ] : @[ ];
}

- (CALayer *)_thumbnailLayer
{
    if (!self.layer.sublayers.count)
        return nil;

    return [self.layer.sublayers objectAtIndex:0];
}

@end

#endif // PLATFORM(MAC)

#endif // WK_API_ENABLED
