#include "net_datatypes.h"
#include <cstring>
#include <string>
#include <vector>
#include <locale>
#include <codecvt>

#ifdef _WIN32
	#include <winsock2.h>	// this needs to be included after common.h, which sets some defines and then includes windows.h, since winsock2.h needs windows.h
#else
    #include <netinet/in.h>
#endif


nword::operator word () const noexcept
{
    return ntohs(m_val);
}

nword& nword::operator = (word val) noexcept
{
    m_val = htons(val);
    return (*this);
}


ndword::operator dword () const noexcept
{
    return ntohl(m_val);
}

ndword& ndword::operator = (dword val) noexcept
{
    m_val = htonl(val);
    return (*this);
}


static int CvtSystemToUTF16(wchar& wChar, lpctstr pInp, int iSizeInBytes)
{
    // Convert a UTF8 encoded string to a single unicode char.
    // RETURN: The length used from input string. < iSizeInBytes

    // bytes bits representation
    // 1 7	0bbbbbbb
    // 2 11 110bbbbb 10bbbbbb
    // 3 16 1110bbbb 10bbbbbb 10bbbbbb
    // 4 21 11110bbb 10bbbbbb 10bbbbbb 10bbbbbb

    byte ch = (byte)*pInp;
    ASSERT(ch >= 0x80);	// needs special UTF8 decoding.

    int iBytes;
    int iStartBits;
    if ((ch & 0xe0) == 0x0c0) // 2 bytes
    {
        iBytes = 2;
        iStartBits = 5;
    }
    else if ((ch & 0xf0) == 0x0e0) // 3 bytes
    {
        iBytes = 3;
        iStartBits = 4;
    }
    else if ((ch & 0xf8) == 0x0f0) // 3 bytes
    {
        iBytes = 4;
        iStartBits = 3;
    }
    else
    {
        return -1;	// invalid format !
    }

    if (iBytes > iSizeInBytes)	// not big enough to hold it.
        return 0;

    wchar wCharTmp = ch & ((1 << iStartBits) - 1);
    int iInp = 1;
    for (; iInp < iBytes; iInp++)
    {
        ch = (byte)pInp[iInp];
        if ((ch & 0xc0) != 0x80)	// bad coding.
            return -1;
        wCharTmp <<= 6;
        wCharTmp |= ch & 0x3f;
    }

    wChar = wCharTmp;
    return iBytes;
}

int CvtSystemToNETUTF16(nachar* pOut, int iSizeOutChars, lpctstr pInp, int iSizeInBytes)
{
    //
    // Convert the system default text format UTF8 to UNICODE
    // May be network byte order !
    // Add null.
    // ARGS:
    //   iSizeInBytes = size ofthe input string. -1 = null terminated.
    // RETURN:
    //  Number of wide chars. not including null.
    //

    ASSERT(pOut);
    ASSERT(pInp);
    if (iSizeOutChars <= 0)
        return -1;

    if (iSizeInBytes <= -1)
    {
        iSizeInBytes = (int)strlen(pInp);
    }
    if (iSizeInBytes <= 0)
    {
        pOut[0] = '\0';
        return 0;
    }

    --iSizeOutChars;

    int iOut = 0;

#ifdef _WIN32
    const OSVERSIONINFO* posInfo = Sphere_GetOSInfo();
    if (posInfo->dwPlatformId == VER_PLATFORM_WIN32_NT ||
        posInfo->dwMajorVersion > 4)
    {
        const int iOutTmp = MultiByteToWideChar(
            CP_UTF8,			// code page
            0,					// character-type options
            pInp,				// address of string to map
            iSizeInBytes,		// number of bytes in string
            reinterpret_cast<lpwstr>(pOut),  // address of wide-character buffer
            iSizeOutChars		// size of buffer
        );

        if (iOutTmp <= 0)
        {
            pOut[0] = '\0';
            return 0;
        }
        if (iOutTmp > iSizeOutChars)	// this should never happen !
        {
            pOut[0] = '\0';
            return 0;
        }

        // flip all the words to network order .
        for (; iOut < iOutTmp; ++iOut)
        {
            pOut[iOut] = *(reinterpret_cast<wchar*>(&(pOut[iOut])));
        }
    }
    else
#endif // _WIN32
    {
        // Win95 or Linux
        int iInp = 0;
        for (; iInp < iSizeInBytes; )
        {
            byte ch = (byte)pInp[iInp];
            if (ch == 0)
                break;

            if (iOut >= iSizeOutChars)
                break;

            if (ch >= 0x80)	// special UTF8 encoded char.
            {
                wchar wChar;
                int iInpTmp = CvtSystemToUTF16(wChar, pInp + iInp, iSizeInBytes - iInp);
                if (iInpTmp <= 0)
                {
                    break;
                }
                pOut[iOut] = wChar;
                iInp += iInpTmp;
            }
            else
            {
                pOut[iOut] = ch;
                ++iInp;
            }

            ++iOut;
        }
    }

    pOut[iOut] = '\0';
    return iOut;
}

int CvtNETUTF16ToSystem(tchar* pOut, int iSizeOutBytes, const nachar* pInp, int iSizeInChars)
{
    // 参数安全检查
    if (iSizeOutBytes <= 0 || !pOut)
        return 0; // 返回 0 而不是 -1，保持原逻辑一致性（原代码出错往往返回0或截断）

    if (!pInp || iSizeInChars == 0)
    {
        pOut[0] = 0;
        return 0;
    }

    // 1. 确定输入长度并构建本地序的 UTF-16 字符串
    // nachar 会自动处理 ntohs (网络字节序 -> 主机字节序)
    if (iSizeInChars < 0)
    {
        // 寻找 null 结尾
        iSizeInChars = 0;
        while (pInp[iSizeInChars] != 0) iSizeInChars++;
    }

    std::u16string utf16Str;
    utf16Str.reserve(iSizeInChars);
    for (int i = 0; i < iSizeInChars; ++i)
    {
        utf16Str.push_back((word)pInp[i]); // 这里的强制转换会触发 nachar 的 operator word()，执行 ntohs
    }

    try 
    {
        // 2. 核心转换：UTF-16 -> UTF-8
        // std::codecvt_utf8_utf16 负责将 char16_t 序列转换为 char 序列 (UTF-8)
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
        
        // 执行转换
        std::string utf8Str = converter.to_bytes(utf16Str);

        // 3. 安全拷贝到输出缓冲区
        size_t copyLen = utf8Str.length();
        if (copyLen >= (size_t)iSizeOutBytes)
        {
            copyLen = iSizeOutBytes - 1; // 截断以保留 null 结尾空间
        }

        if (copyLen > 0)
        {
            memcpy(pOut, utf8Str.data(), copyLen);
        }
        
        pOut[copyLen] = 0; // 确保 null 结尾
        return (int)copyLen;
    }
    catch (...)
    {
        // 转换失败保底
        pOut[0] = 0;
        return 0;
    }
}
