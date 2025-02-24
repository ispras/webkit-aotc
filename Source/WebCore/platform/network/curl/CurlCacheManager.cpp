/*
 * Copyright (C) 2013 University of Szeged
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY UNIVERSITY OF SZEGED ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL UNIVERSITY OF SZEGED OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if USE(CURL)

#include "CurlCacheManager.h"

#include "FileSystem.h"
#include "HTTPHeaderMap.h"
#include "Logging.h"
#include "ResourceHandleClient.h"
#include "ResourceHandleInternal.h"
#include "ResourceRequest.h"
#include <wtf/HashMap.h>
#include <wtf/text/CString.h>

#define IO_BUFFERSIZE 4096

namespace WebCore {

CurlCacheManager& CurlCacheManager::getInstance()
{
    static CurlCacheManager instance;
    return instance;
}

CurlCacheManager::CurlCacheManager()
    : m_disabled(true)
    , m_currentStorageSize(0)
    , m_storageSizeLimit(52428800) // 50 * 1024 * 1024 bytes
{
    // Call setCacheDirectory() to enable the Cache Manager
}

CurlCacheManager::~CurlCacheManager()
{
    if (m_disabled)
        return;

    saveIndex();
}

void CurlCacheManager::setCacheDirectory(const String& directory)
{
    m_cacheDir = directory;
    m_cacheDir.append("/");
    if (m_cacheDir.isEmpty()) {
        LOG(Network, "Cache Error: Cache location is not set! CacheManager disabled.\n");
        m_disabled = true;
        return;
    }

    if (!fileExists(m_cacheDir)) {
        if (!makeAllDirectories(m_cacheDir)) {
            LOG(Network, "Cache Error: Could not open or create cache directory! CacheManager disabled.\n");
            m_disabled = true;
            return;
        }
    }

    m_disabled = false;
    loadIndex();
}

void CurlCacheManager::setStorageSizeLimit(size_t sizeLimit)
{
    m_storageSizeLimit = sizeLimit;
}

void CurlCacheManager::loadIndex()
{
    if (m_disabled)
        return;

    String indexFilePath(m_cacheDir);
    indexFilePath.append("index.dat");

    PlatformFileHandle indexFile = openFile(indexFilePath, OpenForRead);
    if (!isHandleValid(indexFile)) {
        LOG(Network, "Cache Warning: Could not open %s for read\n", indexFilePath.latin1().data());
        return;
    }

    long long filesize = -1;
    if (!getFileSize(indexFilePath, filesize)) {
        LOG(Network, "Cache Error: Could not get file size of %s\n", indexFilePath.latin1().data());
        return;
    }

    // Load the file content into buffer
    Vector<char> buffer;
    buffer.resize(filesize);
    int bufferPosition = 0;
    int bufferReadSize = IO_BUFFERSIZE;
    while (filesize > bufferPosition) {
        if (filesize - bufferPosition < bufferReadSize)
            bufferReadSize = filesize - bufferPosition;

        readFromFile(indexFile, buffer.data() + bufferPosition, bufferReadSize);
        bufferPosition += bufferReadSize;
    }
    closeFile(indexFile);

    // Create strings from buffer
    String headerContent = String(buffer.data());
    Vector<String> indexURLs;
    headerContent.split("\n", indexURLs);
    buffer.clear();

    // Add entries to index
    Vector<String>::const_iterator it = indexURLs.begin();
    Vector<String>::const_iterator end = indexURLs.end();
    if (indexURLs.size() > 1)
        --end; // Last line is empty
    while (it != end) {
        String url = it->stripWhiteSpace();
        std::unique_ptr<CurlCacheEntry> cacheEntry(new CurlCacheEntry(url, m_cacheDir));

        if (cacheEntry->isCached() && cacheEntry->entrySize() < m_storageSizeLimit) {
            m_currentStorageSize += cacheEntry->entrySize();
            makeRoomForNewEntry();
            m_LRUEntryList.prependOrMoveToFirst(url);
            m_index.set(url, std::move(cacheEntry));
        } else
            cacheEntry->invalidate();

        ++it;
    }
}

void CurlCacheManager::saveIndex()
{
    if (m_disabled)
        return;

    String indexFilePath(m_cacheDir);
    indexFilePath.append("index.dat");

    deleteFile(indexFilePath);
    PlatformFileHandle indexFile = openFile(indexFilePath, OpenForWrite);
    if (!isHandleValid(indexFile)) {
        LOG(Network, "Cache Error: Could not open %s for write\n", indexFilePath.latin1().data());
        return;
    }

    auto it = m_LRUEntryList.begin();
    const auto& end = m_LRUEntryList.end();
    while (it != end) {
        const CString& urlLatin1 = it->latin1();
        writeToFile(indexFile, urlLatin1.data(), urlLatin1.length());
        writeToFile(indexFile, "\n", 1);
        ++it;
    }

    closeFile(indexFile);
}

void CurlCacheManager::makeRoomForNewEntry()
{
    if (m_disabled)
        return;

    while (m_currentStorageSize > m_storageSizeLimit) {
        ASSERT(m_index.find(m_LRUEntryList.last()) != m_index.end());
        invalidateCacheEntry(m_LRUEntryList.last());
    }
}

void CurlCacheManager::didReceiveResponse(ResourceHandle* job, ResourceResponse& response)
{
    if (m_disabled)
        return;

    String url = job->firstRequest().url().string();

    if (response.httpStatusCode() == 304) {
        readCachedData(url, job, response);
        m_LRUEntryList.prependOrMoveToFirst(url);
    }
    else if (response.httpStatusCode() == 200) {
        invalidateCacheEntry(url); // Invalidate existing entry on 200

        std::unique_ptr<CurlCacheEntry> cacheEntry(new CurlCacheEntry(url, m_cacheDir));
        bool cacheable = cacheEntry->parseResponseHeaders(response);
        if (cacheable) {
            m_LRUEntryList.prependOrMoveToFirst(url);
            m_index.set(url, std::move(cacheEntry));
            saveResponseHeaders(url, response);
        }
    } else
        invalidateCacheEntry(url);
}

void CurlCacheManager::didFinishLoading(const String& url)
{
    if (m_disabled)
        return;

    HashMap<String, std::unique_ptr<CurlCacheEntry>>::iterator it = m_index.find(url);
    if (it != m_index.end())
        it->value->didFinishLoading();
}

bool CurlCacheManager::isCached(const String& url)
{
    if (m_disabled)
        return false;

    HashMap<String, std::unique_ptr<CurlCacheEntry>>::iterator it = m_index.find(url);
    if (it != m_index.end()) {
        if (it->value->isCached())
            return true;

        invalidateCacheEntry(url);
    }
    return false;
}

HTTPHeaderMap& CurlCacheManager::requestHeaders(const String& url)
{
    ASSERT(isCached(url));
    return m_index.find(url)->value->requestHeaders();
}

void CurlCacheManager::didReceiveData(const String& url, const char* data, size_t size)
{
    if (m_disabled)
        return;

    HashMap<String, std::unique_ptr<CurlCacheEntry>>::iterator it = m_index.find(url);
    if (it != m_index.end()) {
        if (!it->value->saveCachedData(data, size))
            invalidateCacheEntry(url);

        else {
            m_currentStorageSize += size;
            m_LRUEntryList.prependOrMoveToFirst(url);
            makeRoomForNewEntry();
        }
    }
}

void CurlCacheManager::saveResponseHeaders(const String& url, ResourceResponse& response)
{
    if (m_disabled)
        return;

    HashMap<String, std::unique_ptr<CurlCacheEntry>>::iterator it = m_index.find(url);
    if (it != m_index.end())
        if (!it->value->saveResponseHeaders(response))
            invalidateCacheEntry(url);
}

void CurlCacheManager::invalidateCacheEntry(const String& url)
{
    if (m_disabled)
        return;

    HashMap<String, std::unique_ptr<CurlCacheEntry>>::iterator it = m_index.find(url);
    if (it != m_index.end()) {
        if (m_currentStorageSize < it->value->entrySize())
            m_currentStorageSize = 0;
        else
            m_currentStorageSize -= it->value->entrySize();

        it->value->invalidate();
        m_LRUEntryList.remove(url);
        m_index.remove(url);
    }
}

void CurlCacheManager::didFail(const String& url)
{
    invalidateCacheEntry(url);
}

void CurlCacheManager::readCachedData(const String& url, ResourceHandle* job, ResourceResponse& response)
{
    if (m_disabled)
        return;

    HashMap<String, std::unique_ptr<CurlCacheEntry>>::iterator it = m_index.find(url);
    if (it != m_index.end()) {
        it->value->setResponseFromCachedHeaders(response);
        m_LRUEntryList.prependOrMoveToFirst(url);
        if (!it->value->readCachedData(job))
            invalidateCacheEntry(url);
    }
}

}

#endif
