/*
 * Copyright (C) 2006, 2007, 2008, 2010 Apple Inc. All rights reserved.
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "ImageDocument.h"

#include "CachedImage.h"
#include "DocumentLoader.h"
#include "EventListener.h"
#include "EventNames.h"
#include "ExceptionCodePlaceholder.h"
#include "FrameLoader.h"
#include "FrameLoaderClient.h"
#include "FrameView.h"
#include "HTMLHtmlElement.h"
#include "HTMLImageElement.h"
#include "HTMLNames.h"
#include "LocalizedStrings.h"
#include "MainFrame.h"
#include "MouseEvent.h"
#include "NotImplemented.h"
#include "Page.h"
#include "RawDataDocumentParser.h"
#include "RenderElement.h"
#include "ResourceBuffer.h"
#include "Settings.h"

namespace WebCore {

using namespace HTMLNames;

class ImageEventListener : public EventListener {
public:
    static PassRefPtr<ImageEventListener> create(ImageDocument* document) { return adoptRef(new ImageEventListener(document)); }
    static const ImageEventListener* cast(const EventListener* listener)
    {
        return listener->type() == ImageEventListenerType
            ? static_cast<const ImageEventListener*>(listener)
            : 0;
    }

    virtual bool operator==(const EventListener& other) override;

private:
    ImageEventListener(ImageDocument* document)
        : EventListener(ImageEventListenerType)
        , m_doc(document)
    {
    }

    virtual void handleEvent(ScriptExecutionContext*, Event*) override;

    ImageDocument* m_doc;
};
    
class ImageDocumentParser final : public RawDataDocumentParser {
public:
    static PassRefPtr<ImageDocumentParser> create(ImageDocument& document)
    {
        return adoptRef(new ImageDocumentParser(document));
    }

    ImageDocument* document() const
    {
        return toImageDocument(RawDataDocumentParser::document());
    }
    
private:
    ImageDocumentParser(ImageDocument& document)
        : RawDataDocumentParser(document)
    {
    }

    virtual void appendBytes(DocumentWriter&, const char*, size_t) override;
    virtual void finish() override;
};

class ImageDocumentElement final : public HTMLImageElement {
public:
    static PassRefPtr<ImageDocumentElement> create(ImageDocument&);

private:
    ImageDocumentElement(ImageDocument& document)
        : HTMLImageElement(imgTag, document)
        , m_imageDocument(&document)
    {
    }

    virtual ~ImageDocumentElement();
    virtual void didMoveToNewDocument(Document* oldDocument) override;

    ImageDocument* m_imageDocument;
};

inline PassRefPtr<ImageDocumentElement> ImageDocumentElement::create(ImageDocument& document)
{
    return adoptRef(new ImageDocumentElement(document));
}

// --------

static float pageZoomFactor(const Document* document)
{
    Frame* frame = document->frame();
    return frame ? frame->pageZoomFactor() : 1;
}

void ImageDocumentParser::appendBytes(DocumentWriter&, const char*, size_t)
{
    Frame* frame = document()->frame();
    if (!frame->loader().client().allowImage(frame->settings().areImagesEnabled(), document()->url()))
        return;

    CachedImage* cachedImage = document()->cachedImage();
    RefPtr<ResourceBuffer> resourceData = frame->loader().documentLoader()->mainResourceData();
    cachedImage->addDataBuffer(resourceData.get());

    document()->imageUpdated();
}

void ImageDocumentParser::finish()
{
    if (!isStopped() && document()->imageElement()) {
        CachedImage* cachedImage = document()->cachedImage();
        RefPtr<ResourceBuffer> data = document()->frame()->loader().documentLoader()->mainResourceData();

        // If this is a multipart image, make a copy of the current part, since the resource data
        // will be overwritten by the next part.
        if (document()->frame()->loader().documentLoader()->isLoadingMultipartContent())
            data = data->copy();

        cachedImage->finishLoading(data.get());
        cachedImage->finish();

        cachedImage->setResponse(document()->frame()->loader().documentLoader()->response());

        // Report the natural image size in the page title, regardless of zoom level.
        // At a zoom level of 1 the image is guaranteed to have an integer size.
        IntSize size = flooredIntSize(cachedImage->imageSizeForRenderer(document()->imageElement()->renderer(), 1.0f));
        if (size.width()) {
            // Compute the title, we use the decoded filename of the resource, falling
            // back on the (decoded) hostname if there is no path.
            String fileName = decodeURLEscapeSequences(document()->url().lastPathComponent());
            if (fileName.isEmpty())
                fileName = document()->url().host();
            document()->setTitle(imageTitle(fileName, size));
        }

        document()->imageUpdated();
    }

    document()->finishedParsing();
}
    
// --------

ImageDocument::ImageDocument(Frame* frame, const URL& url)
    : HTMLDocument(frame, url, ImageDocumentClass)
    , m_imageElement(0)
    , m_imageSizeIsKnown(false)
    , m_didShrinkImage(false)
    , m_shouldShrinkImage(shouldShrinkToFit())
{
    setCompatibilityMode(QuirksMode);
    lockCompatibilityMode();
}
    
PassRefPtr<DocumentParser> ImageDocument::createParser()
{
    return ImageDocumentParser::create(*this);
}

void ImageDocument::createDocumentStructure()
{
    RefPtr<Element> rootElement = Document::createElement(htmlTag, false);
    appendChild(rootElement, IGNORE_EXCEPTION);
    toHTMLHtmlElement(rootElement.get())->insertedByParser();

    if (frame())
        frame()->injectUserScripts(InjectAtDocumentStart);

    RefPtr<Element> body = Document::createElement(bodyTag, false);
    body->setAttribute(styleAttr, "margin: 0px;");
    
    rootElement->appendChild(body, IGNORE_EXCEPTION);
    
    RefPtr<ImageDocumentElement> imageElement = ImageDocumentElement::create(*this);
    
    imageElement->setAttribute(styleAttr, "-webkit-user-select: none");        
    imageElement->setLoadManually(true);
    imageElement->setSrc(url().string());
    
    body->appendChild(imageElement, IGNORE_EXCEPTION);
    
    if (shouldShrinkToFit()) {
        // Add event listeners
        RefPtr<EventListener> listener = ImageEventListener::create(this);
        if (DOMWindow* domWindow = this->domWindow())
            domWindow->addEventListener("resize", listener, false);
        imageElement->addEventListener("click", listener.release(), false);
#if PLATFORM(IOS)
        // Set the viewport to be in device pixels (rather than the default of 980).
        processViewport(ASCIILiteral("width=device-width"), ViewportArguments::ImageDocument);
#endif
    }

    m_imageElement = imageElement.get();
}

float ImageDocument::scale() const
{
#if PLATFORM(IOS)
    // On iOS big images are subsampled to make them smaller. So, don't resize them.
    return 1;
#else
    if (!m_imageElement)
        return 1;

    FrameView* view = frame()->view();
    if (!view)
        return 1;

    LayoutSize imageSize = m_imageElement->cachedImage()->imageSizeForRenderer(m_imageElement->renderer(), pageZoomFactor(this));
    LayoutSize windowSize = LayoutSize(view->width(), view->height());

    float widthScale = static_cast<float>(windowSize.width()) / imageSize.width();
    float heightScale = static_cast<float>(windowSize.height()) / imageSize.height();

    return std::min(widthScale, heightScale);
#endif
}

void ImageDocument::resizeImageToFit()
{
#if PLATFORM(IOS)
    // On iOS big images are subsampled to make them smaller. So, don't resize them.
#else
    if (!m_imageElement)
        return;

    LayoutSize imageSize = m_imageElement->cachedImage()->imageSizeForRenderer(m_imageElement->renderer(), pageZoomFactor(this));

    float scale = this->scale();
    m_imageElement->setWidth(static_cast<int>(imageSize.width() * scale));
    m_imageElement->setHeight(static_cast<int>(imageSize.height() * scale));

    m_imageElement->setInlineStyleProperty(CSSPropertyCursor, CSSValueWebkitZoomIn);
#endif
}

void ImageDocument::imageClicked(int x, int y)
{
#if PLATFORM(IOS)
    // On iOS big images are subsampled to make them smaller. So, don't resize them.
    UNUSED_PARAM(x);
    UNUSED_PARAM(y);
#else
    if (!m_imageSizeIsKnown || imageFitsInWindow())
        return;

    m_shouldShrinkImage = !m_shouldShrinkImage;

    if (m_shouldShrinkImage)
        windowSizeChanged();
    else {
        restoreImageSize();

        updateLayout();

        float scale = this->scale();

        int scrollX = static_cast<int>(x / scale - (float)frame()->view()->width() / 2);
        int scrollY = static_cast<int>(y / scale - (float)frame()->view()->height() / 2);

        frame()->view()->setScrollPosition(IntPoint(scrollX, scrollY));
    }
#endif
}

void ImageDocument::imageUpdated()
{
    ASSERT(m_imageElement);
    
    if (m_imageSizeIsKnown)
        return;

    if (m_imageElement->cachedImage()->imageSizeForRenderer(m_imageElement->renderer(), pageZoomFactor(this)).isEmpty())
        return;
    
    m_imageSizeIsKnown = true;
    
    if (shouldShrinkToFit()) {
        // Force resizing of the image
        windowSizeChanged();
    }
}

void ImageDocument::restoreImageSize()
{
    if (!m_imageElement || !m_imageSizeIsKnown)
        return;
    
    LayoutSize imageSize = m_imageElement->cachedImage()->imageSizeForRenderer(m_imageElement->renderer(), pageZoomFactor(this));
    m_imageElement->setWidth(imageSize.width());
    m_imageElement->setHeight(imageSize.height());
    
    if (imageFitsInWindow())
        m_imageElement->removeInlineStyleProperty(CSSPropertyCursor);
    else
        m_imageElement->setInlineStyleProperty(CSSPropertyCursor, CSSValueWebkitZoomOut);
        
    m_didShrinkImage = false;
}

bool ImageDocument::imageFitsInWindow() const
{
    if (!m_imageElement)
        return true;

    FrameView* view = frame()->view();
    if (!view)
        return true;

    LayoutSize imageSize = m_imageElement->cachedImage()->imageSizeForRenderer(m_imageElement->renderer(), pageZoomFactor(this));
#if PLATFORM(IOS)
    LayoutSize windowSize = view->contentsToScreen(view->visibleContentRect()).size();
#else
    LayoutSize windowSize = LayoutSize(view->width(), view->height());
#endif
    return imageSize.width() <= windowSize.width() && imageSize.height() <= windowSize.height();
}

void ImageDocument::windowSizeChanged()
{
    if (!m_imageElement || !m_imageSizeIsKnown)
        return;

    bool fitsInWindow = imageFitsInWindow();

#if PLATFORM(IOS)
    if (fitsInWindow)
        return;

    LayoutSize imageSize = m_imageElement->cachedImage()->imageSizeForRenderer(m_imageElement->renderer(), pageZoomFactor(this));
    LayoutRect visibleScreenSize = frame()->view()->contentsToScreen(frame()->view()->visibleContentRect());

    float widthScale = static_cast<float>(visibleScreenSize.width()) / imageSize.width();
    float heightScale = static_cast<float>(visibleScreenSize.height()) / imageSize.height();
    if (widthScale < heightScale)
        processViewport(String::format("width=%d", imageSize.width().toInt()), ViewportArguments::ImageDocument);
    else
        processViewport(String::format("width=%d", static_cast<int>(1.0f + (1.0f - heightScale)) * imageSize.width().toInt()), ViewportArguments::ImageDocument);
#else
    // If the image has been explicitly zoomed in, restore the cursor if the image fits
    // and set it to a zoom out cursor if the image doesn't fit
    if (!m_shouldShrinkImage) {
        if (fitsInWindow)
            m_imageElement->removeInlineStyleProperty(CSSPropertyCursor);
        else
            m_imageElement->setInlineStyleProperty(CSSPropertyCursor, CSSValueWebkitZoomOut);
        return;
    }

    if (m_didShrinkImage) {
        // If the window has been resized so that the image fits, restore the image size
        // otherwise update the restored image size.
        if (fitsInWindow)
            restoreImageSize();
        else
            resizeImageToFit();
    } else {
        // If the image isn't resized but needs to be, then resize it.
        if (!fitsInWindow) {
            resizeImageToFit();
            m_didShrinkImage = true;
        }
    }
#endif
}

CachedImage* ImageDocument::cachedImage()
{ 
    if (!m_imageElement)
        createDocumentStructure();
    
    return m_imageElement->cachedImage();
}

bool ImageDocument::shouldShrinkToFit() const
{
    return frame()->settings().shrinksStandaloneImagesToFit() && frame()->isMainFrame();
}

void ImageEventListener::handleEvent(ScriptExecutionContext*, Event* event)
{
    if (event->type() == eventNames().resizeEvent)
        m_doc->windowSizeChanged();
    else if (event->type() == eventNames().clickEvent && event->isMouseEvent()) {
        MouseEvent* mouseEvent = toMouseEvent(event);
        m_doc->imageClicked(mouseEvent->x(), mouseEvent->y());
    }
}

bool ImageEventListener::operator==(const EventListener& listener)
{
    if (const ImageEventListener* imageEventListener = ImageEventListener::cast(&listener))
        return m_doc == imageEventListener->m_doc;
    return false;
}

// --------

ImageDocumentElement::~ImageDocumentElement()
{
    if (m_imageDocument)
        m_imageDocument->disconnectImageElement();
}

void ImageDocumentElement::didMoveToNewDocument(Document* oldDocument)
{
    if (m_imageDocument) {
        m_imageDocument->disconnectImageElement();
        m_imageDocument = 0;
    }
    HTMLImageElement::didMoveToNewDocument(oldDocument);
}

}
