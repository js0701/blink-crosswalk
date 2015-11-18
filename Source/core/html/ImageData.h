/*
 * Copyright (C) 2008, 2009 Apple Inc. All rights reserved.
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
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ImageData_h
#define ImageData_h

#include "base/task/cancelable_task_tracker.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "core/CoreExport.h"
#include "core/dom/DOMTypedArray.h"
#include "core/fileapi/FileReaderLoader.h"
#include "core/fileapi/FileReaderLoaderClient.h"
#include "core/loader/ImageLoader.h"
#include "platform/geometry/IntSize.h"
#include "platform/heap/Handle.h"
#include "platform/WebThreadSupportingGC.h"
#include "wtf/RefCounted.h"
#include "wtf/RefPtr.h"
#include "wtf/OwnPtr.h"
#include "wtf/ThreadingPrimitives.h"
#include "wtf/ThreadSafeRefCounted.h"

namespace blink {

class ExceptionState;
class Dictionary;
class Blob;
class BlobDataHandle;
class HTMLImageElement;
class ScriptPromise;
class ScriptState;
class ScriptPromiseResolver;
class ImageFrameGenerator;
class SharedBuffer;
class FileReaderLoader;
class ExecutionContext;
class ImageData;

class ImageDataLoaderClient : public ImageLoaderClient
{
public:
    ImageDataLoaderClient(ImageData* imageData, HTMLImageElement* imageElement);
    virtual ~ImageDataLoaderClient();
    virtual void notifyImageSourceChanged() {};
    virtual void notifyImageLoaded(bool isLoadSuccessful);
private:
    ImageData*           m_imageData;
    HTMLImageElement*    m_imageElement;
};

class ImageDataFileReaderLoaderClient: public FileReaderLoaderClient
{

public:
    ImageDataFileReaderLoaderClient(ImageData* imageData);
    virtual ~ImageDataFileReaderLoaderClient();
    virtual void didStartLoading() {};
    virtual void didReceiveData() {};
    virtual void didFinishLoading();
    virtual void didFail(FileError::ErrorCode);

private:
    ImageData* m_imageData;
    FileReaderLoader* m_fileReaderLoader;

};


class CORE_EXPORT ImageData final : public RefCountedWillBeGarbageCollectedFinalized<ImageData>, public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    static PassRefPtrWillBeRawPtr<ImageData> create(const IntSize&);
    static PassRefPtrWillBeRawPtr<ImageData> create(const IntSize&, PassRefPtr<DOMUint8ClampedArray>);
    static PassRefPtrWillBeRawPtr<ImageData> create(unsigned width, unsigned height, ExceptionState&);
    static PassRefPtrWillBeRawPtr<ImageData> create(DOMUint8ClampedArray*, unsigned width, ExceptionState&);
    static PassRefPtrWillBeRawPtr<ImageData> create(DOMUint8ClampedArray*, unsigned width, unsigned height, ExceptionState&);

    IntSize size() const { return m_size; }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    const DOMUint8ClampedArray* data() const { return m_data.get(); }
    DOMUint8ClampedArray* data() { return m_data.get(); }

    DEFINE_INLINE_TRACE() {visitor->trace(m_resolver); }

    virtual v8::Handle<v8::Object> associateWithWrapper(v8::Isolate*, const WrapperTypeInfo*, v8::Handle<v8::Object> wrapper) override;

    ScriptPromise             toBlob (ScriptState *scriptState, const String& type, const Dictionary& encoderOptions);
    ScriptPromise             toBlob (ScriptState *scriptState, const String& type);
    ScriptPromise             toBlob (ScriptState *scriptState, const Dictionary& encoderOptions);
    ScriptPromise             toBlob (ScriptState *scriptState);
    static ScriptPromise      createFromSource (ScriptState *scriptState, HTMLImageElement* source);
    static ScriptPromise      createFromSource (ScriptState *scriptState, Blob* source);
    static String             canDecodeType   (const String& mimeType);
    static String             canEncodeType   (const String& mimeType);

    virtual ~ImageData();

private:

    enum EncodeType {
      EncodeJPEG,    // jpeg
      EncodePNG,     // png
      EncodeWEBP,    // webp
      EncodeMaxType  //
    };

    friend class ImageDataLoaderClient;
    friend class ImageDataFileReaderLoaderClient;

    explicit ImageData(const IntSize&);
    ImageData(const IntSize&, PassRefPtr<DOMUint8ClampedArray>);
    ImageData(HTMLImageElement* pElement, PassRefPtr<ScriptPromiseResolver> resolver);
    ImageData(Blob* blob, PassRefPtr<ScriptPromiseResolver> resolver);

    ScriptPromise toBlobInternal(ScriptState *scriptState, EncodeType type,  int quality);

    static bool validateConstructorArguments(DOMUint8ClampedArray*, unsigned width, unsigned&, ExceptionState&);
    void        imageCodecCallback(bool isSuccessful, bool isEncoder);
    void        loadDataFailed();
    void        decodeAsync();
    void        encodeAsync();

    void        startAsyncDecoding(int width, int height, PassRefPtr<SharedBuffer> encodedData);
    void        startAsyncEncoding(EncodeType type);
    void        startLoadingBlob(ExecutionContext* executionContext);

    void        imageLoaded(bool isLoadSuccessful);
    void        finishFileReaderLoading();

    static EncodeType decideEncodeType(const String& mimeType);

    IntSize m_size;
    RefPtr<DOMUint8ClampedArray> m_data;

    OwnPtr<ImageDataLoaderClient>              m_imageDataLoaderClient;
    OwnPtr<ImageDataFileReaderLoaderClient>    m_ImageDataFileReaderLoaderClient;

    RefPtr<HTMLImageElement> m_imageElement;
    Blob* m_refedBlob;
    Blob* m_createdBlob;
    RefPtr<BlobDataHandle> m_dataHandle;

    RefPtrWillBeMember<ScriptPromiseResolver> m_resolver;
    RefPtr<ImageFrameGenerator> m_ifGenerator;
    RefPtr<SharedBuffer> m_imageBuffer;

    OwnPtr<FileReaderLoader> m_loader;

    //for encoder
    int m_encoderQuality;
    EncodeType m_encodeType;

    OwnPtr<WebThread> m_thread;
};

} // namespace blink

#endif // ImageData_h
