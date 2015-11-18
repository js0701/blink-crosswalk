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
#include "core/html/ImageData.h"

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

ImageDataLoaderClient:: ImageDataLoaderClient (ImageData* imageData, HTMLImageElement* imageElement)
{
    m_imageData    = imageData;
    m_imageElement = imageElement;
    imageElement->addClient(this);
}

void ImageDataLoaderClient:: notifyImageLoaded(bool isLoadSuccessful)
{
   if(m_imageData) m_imageData->imageLoaded(isLoadSuccessful);

}

ImageDataLoaderClient:: ~ImageDataLoaderClient()
{
   if(m_imageElement)
       m_imageElement->removeClient(this);
}

ImageDataFileReaderLoaderClient:: ImageDataFileReaderLoaderClient(ImageData* imageData)
{
    m_imageData        = imageData;
}

ImageDataFileReaderLoaderClient:: ~ImageDataFileReaderLoaderClient()
{

}

void ImageDataFileReaderLoaderClient::didFinishLoading()
{
   if(m_imageData) m_imageData->finishFileReaderLoading();
}

void ImageDataFileReaderLoaderClient::didFail(FileError::ErrorCode)
{
   if(m_imageData) m_imageData->loadDataFailed();
}

PassRefPtrWillBeRawPtr<ImageData> ImageData::create(const IntSize& size)
{
    Checked<int, RecordOverflow> dataSize = 4;
    dataSize *= size.width();
    dataSize *= size.height();
    if (dataSize.hasOverflowed())
        return nullptr;

    return adoptRefWillBeNoop(new ImageData(size));
}

PassRefPtrWillBeRawPtr<ImageData> ImageData::create(const IntSize& size, PassRefPtr<DOMUint8ClampedArray> byteArray)
{
    Checked<int, RecordOverflow> dataSize = 4;
    dataSize *= size.width();
    dataSize *= size.height();
    if (dataSize.hasOverflowed())
        return nullptr;

    if (dataSize.unsafeGet() < 0
        || static_cast<unsigned>(dataSize.unsafeGet()) > byteArray->length())
        return nullptr;

    return adoptRefWillBeNoop(new ImageData(size, byteArray));
}

PassRefPtrWillBeRawPtr<ImageData> ImageData::create(unsigned width, unsigned height, ExceptionState& exceptionState)
{
    if (!RuntimeEnabledFeatures::imageDataConstructorEnabled()) {
        exceptionState.throwTypeError("Illegal constructor");
        return nullptr;
    }
    if (!width || !height) {
        exceptionState.throwDOMException(IndexSizeError, String::format("The source %s is zero or not a number.", width ? "height" : "width"));
        return nullptr;
    }

    Checked<unsigned, RecordOverflow> dataSize = 4;
    dataSize *= width;
    dataSize *= height;
    if (dataSize.hasOverflowed()) {
        exceptionState.throwDOMException(IndexSizeError, "The requested image size exceeds the supported range.");
        return nullptr;
    }

    return adoptRefWillBeNoop(new ImageData(IntSize(width, height)));
}

bool ImageData::validateConstructorArguments(DOMUint8ClampedArray* data, unsigned width, unsigned& lengthInPixels, ExceptionState& exceptionState)
{
    if (!width) {
        exceptionState.throwDOMException(IndexSizeError, "The source width is zero or not a number.");
        return false;
    }
    ASSERT(data);
    unsigned length = data->length();
    if (!length) {
        exceptionState.throwDOMException(IndexSizeError, "The input data has a zero byte length.");
        return false;
    }
    if (length % 4) {
        exceptionState.throwDOMException(IndexSizeError, "The input data byte length is not a multiple of 4.");
        return false;
    }
    length /= 4;
    if (length % width) {
        exceptionState.throwDOMException(IndexSizeError, "The input data byte length is not a multiple of (4 * width).");
        return false;
    }
    lengthInPixels = length;
    return true;
}

PassRefPtrWillBeRawPtr<ImageData> ImageData::create(DOMUint8ClampedArray* data, unsigned width, ExceptionState& exceptionState)
{
    if (!RuntimeEnabledFeatures::imageDataConstructorEnabled()) {
        exceptionState.throwTypeError("Illegal constructor");
        return nullptr;
    }
    unsigned lengthInPixels = 0;
    if (!validateConstructorArguments(data, width, lengthInPixels, exceptionState)) {
        ASSERT(exceptionState.hadException());
        return nullptr;
    }
    ASSERT(lengthInPixels && width);
    unsigned height = lengthInPixels / width;
    return adoptRefWillBeNoop(new ImageData(IntSize(width, height), data));
}

PassRefPtrWillBeRawPtr<ImageData> ImageData::create(DOMUint8ClampedArray* data, unsigned width, unsigned height, ExceptionState& exceptionState)
{
    if (!RuntimeEnabledFeatures::imageDataConstructorEnabled()) {
        exceptionState.throwTypeError("Illegal constructor");
        return nullptr;
    }
    unsigned lengthInPixels = 0;
    if (!validateConstructorArguments(data, width, lengthInPixels, exceptionState)) {
        ASSERT(exceptionState.hadException());
        return nullptr;
    }
    ASSERT(lengthInPixels && width);
    if (height != lengthInPixels / width) {
        exceptionState.throwDOMException(IndexSizeError, "The input data byte length is not equal to (4 * width * height).");
        return nullptr;
    }
    return adoptRefWillBeNoop(new ImageData(IntSize(width, height), data));
}

v8::Handle<v8::Object> ImageData::associateWithWrapper(v8::Isolate* isolate, const WrapperTypeInfo* wrapperType, v8::Handle<v8::Object> wrapper)
{
    ScriptWrappable::associateWithWrapper(isolate, wrapperType, wrapper);

    if (!wrapper.IsEmpty()) {
        // Create a V8 Uint8ClampedArray object.
        v8::Handle<v8::Value> pixelArray = toV8(m_data.get(), wrapper, isolate);
        // Set the "data" property of the ImageData object to
        // the created v8 object, eliminating the C++ callback
        // when accessing the "data" property.
        if (!pixelArray.IsEmpty())
            wrapper->ForceSet(v8AtomicString(isolate, "data"), pixelArray, v8::ReadOnly);
    }
    return wrapper;
}

ImageData::ImageData(const IntSize& size)
    : m_size(size)
    , m_data(DOMUint8ClampedArray::create(size.width() * size.height() * 4))
{
    m_refedBlob = m_createdBlob = NULL;
}

ImageData::ImageData(HTMLImageElement* pElement, PassRefPtr<ScriptPromiseResolver> resolver)
    : m_size(0,0)
    , m_imageElement(pElement)
    , m_resolver(resolver)
{
    m_refedBlob = m_createdBlob = NULL;
}

ImageData::ImageData(Blob* blob, PassRefPtr<ScriptPromiseResolver> resolver)
    : m_size(0,0)
    , m_refedBlob(blob)
    , m_resolver(resolver)
{
    m_createdBlob = NULL;
}

ImageData::ImageData(const IntSize& size, PassRefPtr<DOMUint8ClampedArray> byteArray)
    : m_size(size)
    , m_data(byteArray)
{
    ASSERT_WITH_SECURITY_IMPLICATION(static_cast<unsigned>(size.width() * size.height() * 4) <= m_data->length());
    m_refedBlob = m_createdBlob = NULL;
}

void ImageData::startLoadingBlob(ExecutionContext* executionContext)
{
    if(!m_refedBlob) return;
    if(!m_resolver) return;

    m_ImageDataFileReaderLoaderClient  = adoptPtr(new ImageDataFileReaderLoaderClient(this));
    m_loader = adoptPtr(new FileReaderLoader(FileReaderLoader::ReadAsArrayBuffer, m_ImageDataFileReaderLoaderClient.get()));
    m_loader->setDataType(m_refedBlob->type());
    m_loader->start(executionContext, m_refedBlob->blobDataHandle());
}

ScriptPromise ImageData::toBlobInternal(ScriptState *scriptState, EncodeType type,  int quality)
{
    PassRefPtr<ScriptPromiseResolver> resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();

    if(m_resolver)
    {
        resolver->reject("Image has pending activities");
        return promise;
    }
    m_resolver = resolver;
    if(!m_data)
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

ScriptPromise   ImageData::toBlob (ScriptState *scriptState, const String& type, const Dictionary& encodeOptions)
{
    EncodeType encodeType = decideEncodeType(type);
    int quality;

    if(!DictionaryHelper::get(encodeOptions, "quality", quality))
        quality = DEFAULT_ENCODE_QUALITY;

    return toBlobInternal(scriptState, encodeType, quality);
}

ScriptPromise   ImageData::toBlob (ScriptState *scriptState, const String& type)
{
    EncodeType encodeType = decideEncodeType(type);
    return toBlobInternal(scriptState, encodeType, DEFAULT_ENCODE_QUALITY);
}

ScriptPromise   ImageData::toBlob (ScriptState *scriptState, const Dictionary& encodeOptions)
{
    int quality;
    if(!DictionaryHelper::get(encodeOptions, "quality", quality))
        quality = DEFAULT_ENCODE_QUALITY;
    return toBlobInternal(scriptState, DEFAULT_ENCODE_TYPE, quality);
}

ScriptPromise   ImageData::toBlob (ScriptState *scriptState)
{
    return toBlobInternal(scriptState, DEFAULT_ENCODE_TYPE, DEFAULT_ENCODE_QUALITY);
}

ScriptPromise   ImageData::createFromSource (ScriptState *scriptState, HTMLImageElement* source)
{
    PassRefPtr<ScriptPromiseResolver> resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();

    ImageData *pImageData = new ImageData(source, resolver);
    if(source->complete())
    {
        pImageData->imageLoaded(true);
    }
    else
    {
        pImageData->m_imageDataLoaderClient = adoptPtr(new ImageDataLoaderClient(pImageData, source));
    }

    return promise;
}

ScriptPromise  ImageData::createFromSource  (ScriptState *scriptState, Blob* source)
{
    PassRefPtr<ScriptPromiseResolver> resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();
    ImageData *pImageData = new ImageData(source, resolver);
    pImageData->startLoadingBlob(scriptState->executionContext());

    return promise;
}

void ImageData::imageCodecCallback(bool isSuccessful, bool isEncoder)
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

void ImageData::loadDataFailed()
{
    if(!m_resolver)
    {
        m_resolver->reject("Failed to load image Data");
        m_resolver.clear();
        return;
    }
}

void ImageData::startAsyncDecoding(int width, int height, PassRefPtr<SharedBuffer> encodedData)
{
    if(!m_resolver) return;
    if(m_thread) {
        m_thread.clear();
    }

    m_size.setWidth(width);
    m_size.setHeight(height);

    if(m_data) m_data.clear();
    m_data = DOMUint8ClampedArray::create(width * height * 4);

    m_thread = adoptPtr(Platform::current()->createThread("ImageCodecThread"));
    m_ifGenerator = ImageFrameGenerator::create(SkISize::Make(width,height), encodedData, true);
    m_thread->postTask(FROM_HERE, bind(&ImageData::decodeAsync, this));
}

void ImageData::startAsyncEncoding(EncodeType type)
{
    if(!m_resolver) return;
    if(m_thread) {
        m_thread.clear();
    }
    m_encodeType = type;
    m_thread = adoptPtr(Platform::current()->createThread("ImageCodecThread"));
    m_thread->postTask(FROM_HERE, bind(&ImageData::encodeAsync, this));
}

void ImageData::imageLoaded(bool isLoadSuccessful)
{
    if(!isLoadSuccessful)
        loadDataFailed();
    Image* pImage = m_imageElement->cachedImage()->image();

    PassRefPtr<SharedBuffer> sharedBuffer = RefPtr<SharedBuffer>(pImage->data());
    startAsyncDecoding(pImage->width(), pImage->height(), sharedBuffer);
}

void ImageData::finishFileReaderLoading()
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

String   ImageData::canDecodeType (const String& mimeType)
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

ImageData::EncodeType ImageData::decideEncodeType(const String& mimeType)
{
    if(strcmp(mimeType.utf8().data(), "image/jpeg") == 0)
        return EncodeJPEG;
    if(strcmp(mimeType.utf8().data(), "image/png") == 0)
        return EncodePNG;
    if(strcmp(mimeType.utf8().data(), "image/webp") == 0)
        return EncodeWEBP;
    return EncodeMaxType;
}

String    ImageData::canEncodeType (const String& mimeType)
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

ImageData::~ImageData()
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
}

void   ImageData::decodeAsync()
{
    ASSERT(m_thread->isCurrentThread());

    int rowWidth     = m_size.width()*4;
    void* pixels     = m_data->baseAddress();
    SkImageInfo info = SkImageInfo::Make(m_size.width(), m_size.height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    bool decoded     = m_ifGenerator->decodeAndScale(info, 0, pixels, rowWidth);

    Platform::current()->mainThread()->postTask(FROM_HERE, bind(&ImageData::imageCodecCallback, this, decoded, false));
}

void  ImageData::encodeAsync()
{
    ASSERT(m_thread->isCurrentThread());
    bool encResult = false;

    const ImageDataBuffer mDataBuffer = ImageDataBuffer(m_size, (unsigned char*)(m_data->baseAddress()));
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

    Platform::current()->mainThread()->postTask(FROM_HERE, bind(&ImageData::imageCodecCallback, this, encResult, true));
}
} // namespace blink
