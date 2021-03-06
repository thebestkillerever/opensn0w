/* opensn0w
 * An open-source jailbreaking utility.
 * Brought to you by rms, acfrazier & Maximus
 * Special thanks to iH8sn0w & MuscleNerd
 * libpartial is Copyright (C) 2010 David Wang
 *
 * this version of libpartial has been modified by:
 * Copyright (C) 2010 Joshua Hill
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _WIN32
#undef DATADIR
#endif

#include <curl/curl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <zlib.h>
#include <libgen.h>

#ifdef __cplusplus
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

//#include "core.h"
#include "libpartial.h"
#include "dprint.h"


static size_t count = 0;


//char endianness = IS_BIG_ENDIAN;
extern char endianness;

int
download_file_from_zip(const char *url, const char *path, const char *output,
		       PartialZipProgressCallback progressCallback)
{
	FILE *fd;
	CDFile *file;
	ZipInfo *info;
	unsigned int size;
	unsigned char *data;

	info = PartialZipInit(url);
	if (!info) {
		printf("Cannot find %s\n", url);
		return -1;
	}

	file = PartialZipFindFile(info, path);
	if (!file) {
		printf("Cannot find %s in %s\n", path, url);
		return -1;
	}

	if (progressCallback != NULL) {
		PartialZipSetProgressCallback(info, progressCallback);
	}

	data = PartialZipGetFile(info, file);
	if (!data) {
		printf("Cannot get %s from %s\n", path, url);
		return -1;
	}
	size = file->size;

	fd = fopen(output, "wb");
	if (!fd) {
		printf("Cannot open file %s for output\n", output);
		return -1;
	}

	if (fwrite(data, 1, size, fd) != size) {
		printf("Unable to write entire file to output\n");
		fclose(fd);
		return -1;
	}

	fclose(fd);
	PartialZipRelease(info);
	free(data);
	return 0;
}

static size_t dummyReceive(void *data, size_t size, size_t nmemb, void *info)
{
	return size * nmemb;
}

static size_t
receiveCentralDirectoryEnd(void *data, size_t size, size_t nmemb,
			   ZipInfo * info)
{
	memcpy(info->centralDirectoryEnd + info->centralDirectoryEndRecvd, data,
	       size * nmemb);
	info->centralDirectoryEndRecvd += size * nmemb;
	return size * nmemb;
}

static size_t
receiveCentralDirectory(void *data, size_t size, size_t nmemb, ZipInfo * info)
{
	memcpy(info->centralDirectory + info->centralDirectoryRecvd, data,
	       size * nmemb);
	info->centralDirectoryRecvd += size * nmemb;
	return size * nmemb;
}

static size_t
receiveData(void *data, size_t size, size_t nmemb, void **pFileData)
{
	ZipInfo *info;
	CDFile *file;
	size_t *progress;

	memcpy(pFileData[0], data, size * nmemb);
	pFileData[0] = ((char *)pFileData[0]) + (size * nmemb);
	info = ((ZipInfo *) pFileData[1]);
	file = ((CDFile *) pFileData[2]);
	progress = ((size_t *) pFileData[3]);

	if (progress) {
		count += size * nmemb;
		*progress += size * nmemb;
	}

	if (info && info->progressCallback && file) {
		*progress =
		    ((double)count / (double)file->compressedSize) * 100.0;
		info->progressCallback(info, file, *progress);
	} else if(file) {
		int i = 0;
		double progress2 =
		    ((double)count / (double)file->compressedSize) * 100.0;
		*progress =
                    ((double)count / (double)file->compressedSize) * 100.0;
		if (progress2 < 0) {
			//return;
		}
		if (progress2 > 100) {
			progress2 = 100;
		}

		STATUS("\r[*] Downloading image: [");
		for (i = 0; i < 50; i++) {
			if (i < progress2 / 2) {
				printf("=");
			} else {
				printf(" ");
			}
		}
		printf("] %3.1f%%", progress2);
		if (progress2 == 100) {
			printf("\n");
		}
	}


	return size * nmemb;
}

static CDFile *flipFiles(ZipInfo * info)
{
	char *cur = info->centralDirectory;

	unsigned int i;
	for (i = 0; i < info->centralDirectoryDesc->CDEntries; i++) {
		CDFile *candidate = (CDFile *) cur;
		FLIPENDIANLE(candidate->signature);
		FLIPENDIANLE(candidate->version);
		FLIPENDIANLE(candidate->versionExtract);
		// FLIPENDIANLE(candidate->flags);
		FLIPENDIANLE(candidate->method);
		FLIPENDIANLE(candidate->modTime);
		FLIPENDIANLE(candidate->modDate);
		// FLIPENDIANLE(candidate->crc32);
		FLIPENDIANLE(candidate->compressedSize);
		FLIPENDIANLE(candidate->size);
		FLIPENDIANLE(candidate->lenFileName);
		FLIPENDIANLE(candidate->lenExtra);
		FLIPENDIANLE(candidate->lenComment);
		FLIPENDIANLE(candidate->diskStart);
		// FLIPENDIANLE(candidate->internalAttr);
		// FLIPENDIANLE(candidate->externalAttr);
		FLIPENDIANLE(candidate->offset);

		cur +=
		    sizeof(CDFile) + candidate->lenFileName +
		    candidate->lenExtra + candidate->lenComment;
		return candidate;
	}
	return NULL;
}

ZipInfo *PartialZipInit(const char *url)
{
	char sRange[100];
	char *cur;
	uint64_t start;
	ZipInfo *info = (ZipInfo *) malloc(sizeof(ZipInfo));
	uint64_t end;

	info->url = strdup(url);
	info->centralDirectoryRecvd = 0;
	info->centralDirectoryEndRecvd = 0;
	info->centralDirectoryDesc = NULL;
	info->progressCallback = NULL;

	info->hIPSW = curl_easy_init();

	curl_easy_setopt(info->hIPSW, CURLOPT_URL, info->url);
	curl_easy_setopt(info->hIPSW, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(info->hIPSW, CURLOPT_NOBODY, 1);
	curl_easy_setopt(info->hIPSW, CURLOPT_WRITEFUNCTION, dummyReceive);

	if (strncmp(info->url, "file://", 7) == 0) {
		char path[1024], *filePath;
		FILE *f;
		
		strcpy(path, info->url + 7);

		filePath =
		    (char *)curl_easy_unescape(info->hIPSW, path, 0, NULL);

		f = fopen(filePath, "rb");
		if (!f) {
			curl_free(filePath);
			curl_easy_cleanup(info->hIPSW);
			free(info->url);
			free(info);

			return NULL;
		}

		fseek(f, 0, SEEK_END);
		info->length = ftell(f);
		fclose(f);

		curl_free(filePath);
	} else {
		double dFileLength;

		curl_easy_perform(info->hIPSW);
		curl_easy_getinfo(info->hIPSW, CURLINFO_CONTENT_LENGTH_DOWNLOAD,
				  &dFileLength);
		info->length = dFileLength;
	}

	if (info->length > (0xffff + sizeof(EndOfCD)))
		start = info->length - 0xffff - sizeof(EndOfCD);
	else
		start = 0;

	end = info->length - 1;

	sprintf(sRange, "%" PRIu64 "-%" PRIu64, start, end);

	curl_easy_setopt(info->hIPSW, CURLOPT_WRITEFUNCTION,
			 receiveCentralDirectoryEnd);
	curl_easy_setopt(info->hIPSW, CURLOPT_WRITEDATA, info);
	curl_easy_setopt(info->hIPSW, CURLOPT_RANGE, sRange);
	curl_easy_setopt(info->hIPSW, CURLOPT_HTTPGET, 1);
	curl_easy_perform(info->hIPSW);

	for (cur = info->centralDirectoryEnd;
	     cur < (info->centralDirectoryEnd + (end - start - 1)); cur++) {
		EndOfCD *candidate = (EndOfCD *) cur;
		uint32_t signature = candidate->signature;
		FLIPENDIANLE(signature);
		if (signature == 0x06054b50) {
			uint16_t lenComment = candidate->lenComment;
			FLIPENDIANLE(lenComment);
			if ((cur + lenComment + sizeof(EndOfCD)) ==
			    (info->centralDirectoryEnd +
			     info->centralDirectoryEndRecvd)) {
				FLIPENDIANLE(candidate->diskNo);
				FLIPENDIANLE(candidate->CDDiskNo);
				FLIPENDIANLE(candidate->CDDiskEntries);
				FLIPENDIANLE(candidate->CDEntries);
				FLIPENDIANLE(candidate->CDSize);
				FLIPENDIANLE(candidate->CDOffset);
				FLIPENDIANLE(candidate->lenComment);
				info->centralDirectoryDesc = candidate;
				break;
			}
		}

	}

	if (info->centralDirectoryDesc) {
		info->centralDirectory =
		    (char *)malloc(info->centralDirectoryDesc->CDSize);
		start = info->centralDirectoryDesc->CDOffset;
		end = start + info->centralDirectoryDesc->CDSize - 1;
		sprintf(sRange, "%" PRIu64 "-%" PRIu64, start, end);
		curl_easy_setopt(info->hIPSW, CURLOPT_WRITEFUNCTION,
				 receiveCentralDirectory);
		curl_easy_setopt(info->hIPSW, CURLOPT_WRITEDATA, info);
		curl_easy_setopt(info->hIPSW, CURLOPT_RANGE, sRange);
		curl_easy_setopt(info->hIPSW, CURLOPT_HTTPGET, 1);
		curl_easy_perform(info->hIPSW);

		flipFiles(info);

		return info;
	} else {
		curl_easy_cleanup(info->hIPSW);
		free(info->url);
		free(info);
		return NULL;
	}
}

CDFile *PartialZipFindFile(ZipInfo * info, const char *fileName)
{
	char *cur = info->centralDirectory;
	unsigned int i;
	for (i = 0; i < info->centralDirectoryDesc->CDEntries; i++) {
		CDFile *candidate = (CDFile *) cur;
		char *curFileName = cur + sizeof(CDFile);
		if (strlen(fileName) == candidate->lenFileName
		    && strncmp(fileName, curFileName,
			       candidate->lenFileName) == 0)
			return candidate;

		cur +=
		    sizeof(CDFile) + candidate->lenFileName +
		    candidate->lenExtra + candidate->lenComment;
	}

	return NULL;
}

CDFile *PartialZipListFiles(ZipInfo * info)
{
	char *cur = info->centralDirectory;
	unsigned int i;
	for (i = 0; i < info->centralDirectoryDesc->CDEntries; i++) {
		CDFile *candidate = (CDFile *) cur;
		const char *curFileName = cur + sizeof(CDFile);
		char *myFileName = (char *)malloc(candidate->lenFileName + 1);
		memcpy(myFileName, curFileName, candidate->lenFileName);
		myFileName[candidate->lenFileName] = '\0';

		printf("%s: method: %d, compressed size: %d, size: %d\n",
		       myFileName, candidate->method, candidate->compressedSize,
		       candidate->size);

		free(myFileName);

		cur +=
		    sizeof(CDFile) + candidate->lenFileName +
		    candidate->lenExtra + candidate->lenComment;
	}

	return NULL;
}

unsigned char *PartialZipGetFile(ZipInfo * info, CDFile * file)
{
	LocalFile localHeader;
	LocalFile *pLocalHeader = &localHeader;
	uint64_t start = file->offset;
	uint64_t end = file->offset + sizeof(LocalFile) - 1;
	char sRange[100];
	void *pFileHeader[] = { pLocalHeader, NULL, NULL, NULL };
	unsigned char *fileData;
	size_t progress = 0;
	void *pFileData[] = { NULL, NULL, NULL, NULL };

	count = 0;

	sprintf(sRange, "%" PRIu64 "-%" PRIu64, start, end);

	curl_easy_setopt(info->hIPSW, CURLOPT_URL, info->url);
	curl_easy_setopt(info->hIPSW, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(info->hIPSW, CURLOPT_WRITEFUNCTION, receiveData);
	curl_easy_setopt(info->hIPSW, CURLOPT_WRITEDATA, &pFileHeader);
	curl_easy_setopt(info->hIPSW, CURLOPT_RANGE, sRange);
	curl_easy_setopt(info->hIPSW, CURLOPT_HTTPGET, 1);
	curl_easy_perform(info->hIPSW);

	FLIPENDIANLE(localHeader.signature);
	FLIPENDIANLE(localHeader.versionExtract);
	// FLIPENDIANLE(localHeader.flags);
	FLIPENDIANLE(localHeader.method);
	FLIPENDIANLE(localHeader.modTime);
	FLIPENDIANLE(localHeader.modDate);
	// FLIPENDIANLE(localHeader.crc32);
	FLIPENDIANLE(localHeader.compressedSize);
	FLIPENDIANLE(localHeader.size);
	FLIPENDIANLE(localHeader.lenFileName);
	FLIPENDIANLE(localHeader.lenExtra);

	fileData = (unsigned char *)malloc(file->compressedSize);
	
	pFileData[0] = fileData;
	pFileData[1] = info;
	pFileData[2] = file;
	pFileData[3] = &progress;

	start =
	    file->offset + sizeof(LocalFile) + localHeader.lenFileName +
	    localHeader.lenExtra;
	end = start + file->compressedSize - 1;
	sprintf(sRange, "%" PRIu64 "-%" PRIu64, start, end);

	curl_easy_setopt(info->hIPSW, CURLOPT_WRITEFUNCTION, receiveData);
	curl_easy_setopt(info->hIPSW, CURLOPT_WRITEDATA, pFileData);
	curl_easy_setopt(info->hIPSW, CURLOPT_RANGE, sRange);
	curl_easy_setopt(info->hIPSW, CURLOPT_HTTPGET, 1);
	curl_easy_perform(info->hIPSW);

	if (file->method == 8) {
		unsigned char *uncData = (unsigned char *)malloc(file->size);
		z_stream strm;
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		strm.avail_in = 0;
		strm.next_in = NULL;

		inflateInit2(&strm, -MAX_WBITS);
		strm.avail_in = file->compressedSize;
		strm.next_in = fileData;
		strm.avail_out = file->size;
		strm.next_out = uncData;
		inflate(&strm, Z_FINISH);
		inflateEnd(&strm);
		free(fileData);
		fileData = uncData;
	}
	return fileData;
}

void
PartialZipSetProgressCallback(ZipInfo * info,
			      PartialZipProgressCallback progressCallback)
{
	info->progressCallback = progressCallback;
}

void PartialZipRelease(ZipInfo * info)
{
	curl_easy_cleanup(info->hIPSW);
	free(info->centralDirectory);
	free(info->url);
	free(info);

	curl_global_cleanup();
}
