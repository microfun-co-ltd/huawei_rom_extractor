


#include <stdio.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/stat.h>


struct FileRecord
{
    unsigned int m_ui32Magic; // Magic 0x55 0xAA 0x5A 0xA5
    unsigned int m_ui32HdrLen; // Length of this header include above magic
    unsigned int m_ui32Unk1;
    char m_ai8HwId[8];
    unsigned int m_ui32FileSeq;
    unsigned int m_ui32FileSize;
    char m_ai8FileDate[16];
    char m_ai8FileTime[16];
    char m_ai8FileType[16]; // SYSTEM, BOOT, etc.
    char m_ai8Reserved[16];
    unsigned short m_ui16HdrChecksum;
    unsigned short m_ui16BlkSize;
    unsigned short m_ui16Reserved;
};

static unsigned int const g_ui32RecordMagic = 0xA55AAA55;

FILE * g_pUpdateApp;
FILE * g_pOutFile;
unsigned char g_aui8Buf[1 * 1024 * 1024];
size_t g_stBytesInBuf;
size_t g_stBytesConsumed;

bool fillBuf()
{
    memmove(g_aui8Buf, g_aui8Buf + g_stBytesConsumed, g_stBytesInBuf - g_stBytesConsumed);
    g_stBytesInBuf -= g_stBytesConsumed;
    g_stBytesConsumed = 0;

    size_t stFree = sizeof(g_aui8Buf) - g_stBytesInBuf;
    if (stFree == 0)
    {
        return true;
    }

    size_t stRead = fread(g_aui8Buf + g_stBytesInBuf, 1, stFree, g_pUpdateApp);
    if (stRead <= 0)
    {
        return false;
    }

    g_stBytesInBuf += stRead;

    return true;
}

void skip(size_t stBytes)
{
    // Check if the enitre range is in buffer
    if (stBytes <= g_stBytesInBuf - g_stBytesConsumed)
    { // Entire range is in buffer, just skip in buffer
        g_stBytesConsumed += stBytes;
    }
    else
    { // There are more data outside the buffer to skip, we will not bother to read them, just skip them in file
        fseek(g_pUpdateApp, stBytes - (g_stBytesInBuf - g_stBytesConsumed), SEEK_CUR);

        // Clear the buffer
        g_stBytesConsumed = g_stBytesInBuf;
    }
}

int walk(char const * pcszOutputFileName, size_t stOutputFileNameLen)
{
    if (!fillBuf())
    {
        return 2;
    }

    // Search for
    for (;;)
    {
        // Continue to find if there is a marker
        unsigned char * pui8Char = static_cast<unsigned char *>(memchr(g_aui8Buf + g_stBytesConsumed, 0x55, g_stBytesInBuf - g_stBytesConsumed));
        if (pui8Char == nullptr)
        {
            g_stBytesConsumed = g_stBytesInBuf;
            if (!fillBuf())
            {
                break;
            }
            continue;
        }

        // Discard garbage bytes before the found (possible) marker
        g_stBytesConsumed = pui8Char - g_aui8Buf;

        // If there is not enough data in buffer to form the entire marker, load more data
        if (g_stBytesInBuf - g_stBytesConsumed < 4)
        {
            if (!fillBuf())
            {
                break;
            }
            continue;
        }

        // Compare the marker
        if (memcmp(g_aui8Buf + g_stBytesConsumed, &g_ui32RecordMagic, sizeof(g_ui32RecordMagic)) != 0)
        { // Not a marker, continue to find marker in buffer.
            g_stBytesConsumed++;
            continue;
        }

        //
        // Marker found, may be a file record
        //

        // If the header is not fully in buffer, read more data
        if (g_stBytesInBuf - g_stBytesConsumed < sizeof(FileRecord))
        {
            if (!fillBuf())
            {
                break;
            }
        }

        // Copy he possible file header to local variable because of alignment problem
        FileRecord oRec;
        memcpy(&oRec, g_aui8Buf + g_stBytesConsumed, sizeof(oRec));

        // TODO: Verify it is really a file header
        {
            //unsigned short ui16Checksum = crc16();
        }

        if (pcszOutputFileName == nullptr)
        { // Just skip the file
            printf("%.*s %lu %.*s %.*s\n", sizeof(oRec.m_ai8FileType), oRec.m_ai8FileType, oRec.m_ui32FileSize, sizeof(oRec.m_ai8FileDate), oRec.m_ai8FileDate, sizeof(oRec.m_ai8FileTime), oRec.m_ai8FileTime);

            skip(oRec.m_ui32HdrLen);
            skip(oRec.m_ui32FileSize);

            continue;
        }

        if (memcmp(oRec.m_ai8FileType, pcszOutputFileName, stOutputFileNameLen < sizeof(oRec.m_ai8FileType) ? stOutputFileNameLen + 1 : sizeof(oRec.m_ai8FileType)) != 0)
        { // Not the file we are looking for, skip it
            // NOTE: For now, because we are not sure whether this is really a file header, we'd better not using the m_ui32FileSize field to skip, just skip the marker and continue to search for next marker.
            g_stBytesConsumed += 4;
            continue;
        }

        skip(oRec.m_ui32HdrLen);

        //
        // Found, begin to dump data
        //

        g_pOutFile = fopen(pcszOutputFileName, "wb+");
        if (g_pOutFile == nullptr)
        {
            return 3;
        }


        // Copy data
        for (size_t stTotalWritten = 0; stTotalWritten < oRec.m_ui32FileSize;)
        {
            if (g_stBytesConsumed < g_stBytesInBuf)
            {
                // Compute number of bytes to copy out
                size_t stToWrite = g_stBytesInBuf - g_stBytesConsumed;
                if (stToWrite > oRec.m_ui32FileSize - stTotalWritten)
                {
                    stToWrite = oRec.m_ui32FileSize - stTotalWritten;
                }

                // Write file
                size_t stWritten = fwrite(g_aui8Buf + g_stBytesConsumed, 1, stToWrite, g_pOutFile);
                if (stWritten != stToWrite)
                {
                    return 4;
                }

                stTotalWritten += stWritten;

                g_stBytesConsumed += stWritten;
            }
            else
            {
                if (!fillBuf())
                {
                    return 2;
                }
            }
        }

        fclose(g_pOutFile);
        g_pOutFile = nullptr;

        // If the only file is extracted, stop
        break;
    }

    return 0;
}

int main(int argc, char * argv[])
{
    if (argc < 2)
    {
        printf("%s huawei_rom_file [file_to_extract]\n", argv[0]);
        printf("  huawei_rom_file: usually the UPDATE.APP file\n");
        printf("  file_to_extract: if omitted, list all files in ROM file\n");
        return 1;
    }

    char const * pcszUpdateApp = argv[1];

    char const * pcszOutputFileName = nullptr;
    size_t stOutputFileNameLen = 0;
    if (argc > 2)
    {
        pcszOutputFileName = argv[2];
        stOutputFileNameLen = strlen(pcszOutputFileName);
    }


    g_pUpdateApp = fopen(pcszUpdateApp, "rb");
    if (g_pUpdateApp == nullptr)
    {
        return 1;
    }

    int i32Ret = walk(pcszOutputFileName, stOutputFileNameLen);

    fclose(g_pUpdateApp);

    if (g_pOutFile != nullptr)
    {
        fclose(g_pOutFile);
    }

    return i32Ret;
}