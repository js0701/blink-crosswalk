/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
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

#include "config.h"
#include "core/html/ImageDataHandle.h"

#include "bindings/core/v8/Dictionary.h"
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "bindings/core/v8/ScriptState.h"
#include "bindings/core/v8/V8Uint8ClampedArray.h"
#include "core/dom/ActiveDOMObject.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/ExecutionContext.h"
#include "core/fileapi/Blob.h"
#include "core/html/HTMLImageElement.h"
#include "platform/blob/BlobData.h"
#include "platform/graphics/Image.h"
#include "platform/graphics/ImageBuffer.h"
#include "platform/graphics/ImageSource.h"
#include "platform/graphics/ImageFrameGenerator.h"
#include "platform/image-decoders/ImageDecoder.h"
#include "platform/image-encoders/skia/JPEGImageEncoder.h"
#include "platform/image-encoders/skia/PNGImageEncoder.h"
#include "platform/image-encoders/skia/WEBPImageEncoder.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/SharedBuffer.h"
#include "platform/Task.h"
#include "wtf/MainThread.h"
#include "wtf/ThreadingPrimitives.h"

namespace blink {

#define DEFAULT_ENCODE_QUALITY       80
#define DEFAULT_ENCODE_TYPE          EncodeJPEG

ImageDataLoaderClient:: ImageDataLoaderClient (ImageDataHandle* imageDataHandle, HTMLImageElement* imageElement)
{
    m_imageDataHandle    = imageDataHandle;
    m_imageElement       = imageElement;
    imageElement->addClient(this);
}

void ImageDataLoaderClient:: notifyImageLoaded(bool isLoadSuccessful)
{
   if(m_imageDataHandle) m_imageDataHandle->imageLoaded(isLoadSuccessful);

}

ImageDataLoaderClient:: ~ImageDataLoaderClient()
{
   if(m_imageElement)
       m_imageElement->removeClient(this);
}

ImageDataFileReaderLoaderClient:: ImageDataFileReaderLoaderClient(ImageDataHandle* imageDataHandle)
{
    m_imageDataHandle   = imageDataHandle;
}

ImageDataFileReaderLoaderClient:: ~ImageDataFileReaderLoaderClient()
{

}

void ImageDataFileReaderLoaderClient::didFinishLoading()
{
   if(m_imageDataHandle) m_imageDataHandle->finishFileReaderLoading();
}

void ImageDataFileReaderLoaderClient::didFail(FileError::ErrorCode)
{
   if(m_imageDataHandle) m_imageDataHandle->loadDataFailed();
}

v8::Handle<v8::Object> ImageDataHandle::associateWithWrapper(v8::Isolate* isolate, const WrapperTypeInfo* wrapperType, v8::Handle<v8::Object> wrapper)
{
    ScriptWrappable::associateWithWrapper(isolate, wrapperType, wrapper);
    return wrapper;
}

ImageDataHandle::ImageDataHandle(const IntSize& size)
    : m_size(size)
{
    m_rawData = NULL;
    m_refedBlob = m_createdBlob = NULL;
}

ImageDataHandle::ImageDataHandle(HTMLImageElement* pElement, PassRefPtr<ScriptPromiseResolver> resolver)
    : m_size(0,0)
    , m_imageElement(pElement)
    , m_resolver(resolver)
{
    m_rawData = NULL;
    m_refedBlob = m_createdBlob = NULL;
}

ImageDataHandle::ImageDataHandle(Blob* blob, PassRefPtr<ScriptPromiseResolver> resolver)
    : m_size(0,0)
    , m_refedBlob(blob)
    , m_resolver(resolver)
{
    m_rawData = NULL;
    m_createdBlob = NULL;
}


void ImageDataHandle::startLoadingBlob(ExecutionContext* executionContext)
{
    if(!m_refedBlob) return;
    if(!m_resolver) return;

    m_ImageDataFileReaderLoaderClient  = adoptPtr(new ImageDataFileReaderLoaderClient(this));
    m_loader = adoptPtr(new FileReaderLoader(FileReaderLoader::ReadAsArrayBuffer, m_ImageDataFileReaderLoaderClient.get()));
    m_loader->setDataType(m_refedBlob->type());
    m_loader->start(executionContext, m_refedBlob->blobDataHandle());
}

ScriptPromise ImageDataHandle::toBlobInternal(ScriptState *scriptState, EncodeType type,  int quality)
{
    PassRefPtr<ScriptPromiseResolver> resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();

    if(m_resolver)
    {
        resolver->reject("Image has pending activities");
        return promise;
    }
    m_resolver = resolver;

    if(!getData())
    {
        m_resolver->reject("Image has no pixel data available");
        return promise;
    }

    if(type >= EncodeMaxType)
    {
        m_resolver->reject("Unsupported encode type");
        return promise;
    }

    if(quality < 0 || quality > 100)
    {
        m_resolver->reject("Unsupport quality, should be between 0 and 100");
        return promise;
    }
    m_encoderQuality = quality;

    startAsyncEncoding(type);
    return promise;
}

ScriptPromise  ImageDataHandle::toBlob (ScriptState *scriptState, const String& type, const Dictionary& encodeOptions)
{
    EncodeType encodeType = decideEncodeType(type);
    int quality;

    if(!DictionaryHelper::get(encodeOptions, "quality", quality))
        quality = DEFAULT_ENCODE_QUALITY;

    return toBlobInternal(scriptState, encodeType, quality);
}

ScriptPromise   ImageDataHandle::toBlob (ScriptState *scriptState, const String& type)
{
    EncodeType encodeType = decideEncodeType(type);
    return toBlobInternal(scriptState, encodeType, DEFAULT_ENCODE_QUALITY);
}

ScriptPromise   ImageDataHandle::toBlob (ScriptState *scriptState, const Dictionary& encodeOptions)
{
    int quality;
    if(!DictionaryHelper::get(encodeOptions, "quality", quality))
        quality = DEFAULT_ENCODE_QUALITY;
    return toBlobInternal(scriptState, DEFAULT_ENCODE_TYPE, quality);
}

ScriptPromise   ImageDataHandle::toBlob (ScriptState *scriptState)
{
    return toBlobInternal(scriptState, DEFAULT_ENCODE_TYPE, DEFAULT_ENCODE_QUALITY);
}

ScriptPromise   ImageDataHandle::createFromSource (ScriptState *scriptState, HTMLImageElement* source)
{
    PassRefPtr<ScriptPromiseResolver> resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();

    ImageDataHandle *pImageDataHandle = new ImageDataHandle(source, resolver);
    if(source->complete())
    {
        pImageDataHandle->imageLoaded(true);
    }
    else
    {
        pImageDataHandle->m_imageDataLoaderClient = adoptPtr(new ImageDataLoaderClient(pImageDataHandle, source));
    }

    return promise;
}

ScriptPromise  ImageDataHandle::createFromSource  (ScriptState *scriptState, Blob* source)
{
    PassRefPtr<ScriptPromiseResolver> resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();
    ImageDataHandle *pImageData = new ImageDataHandle(source, resolver);
    pImageData->startLoadingBlob(scriptState->executionContext());

    return promise;
}

void ImageDataHandle::imageCodecCallback(bool isSuccessful, bool isEncoder)
{
   if(m_thread) {
       m_thread.clear();
   }
   if(!m_resolver) return;

   if(isSuccessful)
   {
       if(!isEncoder)
           m_resolver->resolve(adoptRefWillBeNoop(this));
       else
       {
           m_createdBlob = Blob::create(m_dataHandle.release());
           m_resolver->resolve(m_createdBlob);
       }
   }
   else
   {
       m_resolver->reject("Failed to decode image");
       if(m_createdBlob) delete m_createdBlob;
       m_createdBlob = NULL;
   }

   m_resolver.clear();
}

void ImageDataHandle::loadDataFailed()
{
    if(!m_resolver)
    {
        m_resolver->reject("Failed to load image Data");
        m_resolver.clear();
        return;
    }
}

void ImageDataHandle:: prepareRawData()
{
    if(m_rawData) partitionFree(m_rawData);
    m_rawData = partitionAllocGeneric(WTF::Partitions::getBufferPartition(),width() * height() * 4);
}

void ImageDataHandle::startAsyncDecoding(int width, int height, PassRefPtr<SharedBuffer> encodedData)
{
    if(!m_resolver) return;
    if(m_thread) {
        m_thread.clear();
    }

    m_size.setWidth(width);
    m_size.setHeight(height);

    prepareRawData();

    m_thread = adoptPtr(Platform::current()->createThread("ImageCodecThread"));
    m_ifGenerator = ImageFrameGenerator::create(SkISize::Make(width,height), encodedData, true);
    m_thread->postTask(FROM_HERE, bind(&ImageDataHandle::decodeAsync, this));
}

void ImageDataHandle::startAsyncEncoding(EncodeType type)
{
    if(!m_resolver) return;
    if(m_thread) {
        m_thread.clear();
    }
    m_encodeType = type;
    m_thread = adoptPtr(Platform::current()->createThread("ImageCodecThread"));
    m_thread->postTask(FROM_HERE, bind(&ImageDataHandle::encodeAsync, this));
}

void ImageDataHandle::imageLoaded(bool isLoadSuccessful)
{
    if(!isLoadSuccessful)
        loadDataFailed();
    Image* pImage = m_imageElement->cachedImage()->image();

    PassRefPtr<SharedBuffer> sharedBuffer = RefPtr<SharedBuffer>(pImage->data());
    startAsyncDecoding(pImage->width(), pImage->height(), sharedBuffer);
}

void ImageDataHandle::finishFileReaderLoading()
{
    if(!m_resolver) return;

    RefPtr<DOMArrayBuffer> result       = m_loader->arrayBufferResult();
    RefPtr<SharedBuffer>   sharedBuffer = SharedBuffer::create((const char*)(result->data()), result->byteLength());
    OwnPtr<ImageDecoder> decoder = ImageDecoder::create(*(sharedBuffer.get()), ImageSource::AlphaPremultiplied, ImageSource::GammaAndColorProfileApplied);
    if (!decoder)
        loadDataFailed();

    decoder->setData(sharedBuffer.get(), true);
    if (!decoder->isSizeAvailable())
        loadDataFailed();

    const IntSize size = decoder->size();
    startAsyncDecoding(size.width(), size.height(), sharedBuffer.release());
}

String ImageDataHandle::canDecodeType (const String& mimeType)
{
    String supported("always");
    String notSupported("not supported");

    if(strcmp(mimeType.utf8().data(), "image/jpeg") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/png") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/gif") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/webp") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/bmp") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/ico") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/icon") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/vnd.microsoft.icon") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/x-icon") == 0)
        return supported;
    return notSupported;
}

ImageDataHandle::EncodeType ImageDataHandle::decideEncodeType(const String& mimeType)
{
    if(strcmp(mimeType.utf8().data(), "image/jpeg") == 0)
        return EncodeJPEG;
    if(strcmp(mimeType.utf8().data(), "image/png") == 0)
        return EncodePNG;
    if(strcmp(mimeType.utf8().data(), "image/webp") == 0)
        return EncodeWEBP;
    return EncodeMaxType;
}

String  ImageDataHandle::canEncodeType (const String& mimeType)
{
    String supported("always");
    String notSupported("not supported");

    if(strcmp(mimeType.utf8().data(), "image/jpeg") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/png") == 0)
        return supported;
    if(strcmp(mimeType.utf8().data(), "image/webp") == 0)
        return supported;
    return notSupported;
}



ImageDataHandle::~ImageDataHandle()
{
#if !ENABLE(OILPAN)
    if(m_thread)
    {
       m_thread.clear();
    }

    if(m_resolver)
    {
       if(m_createdBlob) delete m_createdBlob;
       m_createdBlob = NULL;
    }
#endif
   if(m_rawData) partitionFree(m_rawData);
   m_rawData = NULL;
}

void   ImageDataHandle::decodeAsync()
{
    ASSERT(m_thread->isCurrentThread());

    int rowWidth     = m_size.width()*4;
    void* pixels     = getData();
    SkImageInfo info = SkImageInfo::Make(m_size.width(), m_size.height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    bool decoded     = m_ifGenerator->decodeAndScale(info, 0, pixels, rowWidth);

    Platform::current()->mainThread()->postTask(FROM_HERE, bind(&ImageDataHandle::imageCodecCallback, this, decoded, false));
}

void  ImageDataHandle::encodeAsync()
{
    ASSERT(m_thread->isCurrentThread());
    bool encResult = false;

    const ImageDataBuffer mDataBuffer = ImageDataBuffer(m_size, (unsigned char*) getData());
    Vector<unsigned char> encodedImage;

    switch(m_encodeType)
    {
       case EncodeJPEG:
           encResult = JPEGImageEncoder::encode(mDataBuffer, m_encoderQuality, &encodedImage);
           break;
       case EncodePNG:
           encResult = PNGImageEncoder::encode(mDataBuffer, &encodedImage);
           break;
       case EncodeWEBP:
           encResult = WEBPImageEncoder::encode(mDataBuffer, m_encoderQuality, &encodedImage);
           break;
       default:
           break;
    }

    if(encResult)
    {
        OwnPtr<BlobData> blobData(BlobData::create());
        blobData->appendBytes(encodedImage.data(), encodedImage.size());
        size_t size = encodedImage.size();
        m_dataHandle = BlobDataHandle::create(blobData.release(),size);
    }

    Platform::current()->mainThread()->postTask(FROM_HERE, bind(&ImageDataHandle::imageCodecCallback, this, encResult, true));
}

void   ImageDataHandle::releaseLocalBuffer()
{
    if(m_rawData)
        partitionFree(m_rawData);
    m_rawData = NULL;
}

} // namespace blink
