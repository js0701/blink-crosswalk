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
#include "ImageDataHandle.h"

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


class CORE_EXPORT ImageData final : public ImageDataHandle {
    DEFINE_WRAPPERTYPEINFO();
public:
    static PassRefPtrWillBeRawPtr<ImageData> create(const IntSize&);
    static PassRefPtrWillBeRawPtr<ImageData> create(const IntSize&, PassRefPtr<DOMUint8ClampedArray>);
    static PassRefPtrWillBeRawPtr<ImageData> create(unsigned width, unsigned height, ExceptionState&);
    static PassRefPtrWillBeRawPtr<ImageData> create(DOMUint8ClampedArray*, unsigned width, ExceptionState&);
    static PassRefPtrWillBeRawPtr<ImageData> create(DOMUint8ClampedArray*, unsigned width, unsigned height, ExceptionState&);

    virtual void*  getData();
    virtual void   releaseLocalBuffer() { } //Do nothing

    const DOMUint8ClampedArray* data() const { return m_data.get(); }
    DOMUint8ClampedArray* data() { return m_data.get(); }

    //DEFINE_INLINE_TRACE() {visitor->trace(m_resolver); }

    virtual v8::Handle<v8::Object> associateWithWrapper(v8::Isolate*, const WrapperTypeInfo*, v8::Handle<v8::Object> wrapper) override;

    static ScriptPromise      createFromSource (ScriptState *scriptState, HTMLImageElement* source);
    static ScriptPromise      createFromSource (ScriptState *scriptState, Blob* source);
    virtual ~ImageData();


protected:
    virtual void prepareRawData();

private:

    explicit ImageData(const IntSize&);
    ImageData(const IntSize&, PassRefPtr<DOMUint8ClampedArray>);
    ImageData(HTMLImageElement* pElement, PassRefPtr<ScriptPromiseResolver> resolver);
    ImageData(Blob* blob, PassRefPtr<ScriptPromiseResolver> resolver);

    static bool validateConstructorArguments(DOMUint8ClampedArray*, unsigned width, unsigned&, ExceptionState&);
    RefPtr<DOMUint8ClampedArray> m_data;

};

} // namespace blink

#endif // ImageData_h
