/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Copyright (C) 2016 Intel Corporation.
** Copyright (C) 2019 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Giuseppe D'Angelo <giuseppe.dangelo@kdab.com>
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qbytearray.h"
#include "qbytearraymatcher.h"
#include "private/qtools_p.h"
#include "qhashfunctions.h"
#include "qlist.h"
#include "qlocale_p.h"
#include "qlocale_tools_p.h"
#include "private/qnumeric_p.h"
#include "private/qsimd_p.h"
#include "qstringalgorithms_p.h"
#include "qscopedpointer.h"
#include "qbytearray_p.h"
#include <qdatastream.h>
#include <qmath.h>

#ifndef QT_NO_COMPRESS
#include <zconf.h>
#include <zlib.h>
#endif
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#define IS_RAW_DATA(d) ((d)->flags() & QArrayData::RawDataType)

QT_BEGIN_NAMESPACE

const char QByteArray::_empty = '\0';

// ASCII case system, used by QByteArray::to{Upper,Lower}() and qstr(n)icmp():
static constexpr inline uchar asciiUpper(uchar c)
{
    return c >= 'a' && c <= 'z' ? c & ~0x20 : c;
}

static constexpr inline uchar asciiLower(uchar c)
{
    return c >= 'A' && c <= 'Z' ? c | 0x20 : c;
}

qsizetype qFindByteArray(
        const char *haystack0, qsizetype haystackLen, qsizetype from,
        const char *needle0, qsizetype needleLen);

/*****************************************************************************
  Safe and portable C string functions; extensions to standard string.h
 *****************************************************************************/

/*! \relates QByteArray

    Returns a duplicate string.

    Allocates space for a copy of \a src, copies it, and returns a
    pointer to the copy. If \a src is \nullptr, it immediately returns
    \nullptr.

    Ownership is passed to the caller, so the returned string must be
    deleted using \c delete[].
*/

char *qstrdup(const char *src)
{
    if (!src)
        return nullptr;
    char *dst = new char[strlen(src) + 1];
    return qstrcpy(dst, src);
}

/*! \relates QByteArray

    Copies all the characters up to and including the '\\0' from \a
    src into \a dst and returns a pointer to \a dst. If \a src is
    \nullptr, it immediately returns \nullptr.

    This function assumes that \a dst is large enough to hold the
    contents of \a src.

    \note If \a dst and \a src overlap, the behavior is undefined.

    \sa qstrncpy()
*/

char *qstrcpy(char *dst, const char *src)
{
    if (!src)
        return nullptr;
#ifdef Q_CC_MSVC
    const int len = int(strlen(src));
    // This is actually not secure!!! It will be fixed
    // properly in a later release!
    if (len >= 0 && strcpy_s(dst, len+1, src) == 0)
        return dst;
    return nullptr;
#else
    return strcpy(dst, src);
#endif
}

/*! \relates QByteArray

    A safe \c strncpy() function.

    Copies at most \a len bytes from \a src (stopping at \a len or the
    terminating '\\0' whichever comes first) into \a dst and returns a
    pointer to \a dst. Guarantees that \a dst is '\\0'-terminated. If
    \a src or \a dst is \nullptr, returns \nullptr immediately.

    This function assumes that \a dst is at least \a len characters
    long.

    \note If \a dst and \a src overlap, the behavior is undefined.

    \sa qstrcpy()
*/

char *qstrncpy(char *dst, const char *src, uint len)
{
    if (!src || !dst)
        return nullptr;
    if (len > 0) {
#ifdef Q_CC_MSVC
        strncpy_s(dst, len, src, len - 1);
#else
        strncpy(dst, src, len);
#endif
        dst[len-1] = '\0';
    }
    return dst;
}

/*! \fn uint qstrlen(const char *str)
    \relates QByteArray

    A safe \c strlen() function.

    Returns the number of characters that precede the terminating '\\0',
    or 0 if \a str is \nullptr.

    \sa qstrnlen()
*/

/*! \fn uint qstrnlen(const char *str, uint maxlen)
    \relates QByteArray
    \since 4.2

    A safe \c strnlen() function.

    Returns the number of characters that precede the terminating '\\0', but
    at most \a maxlen. If \a str is \nullptr, returns 0.

    \sa qstrlen()
*/

/*!
    \relates QByteArray

    A safe \c strcmp() function.

    Compares \a str1 and \a str2. Returns a negative value if \a str1
    is less than \a str2, 0 if \a str1 is equal to \a str2 or a
    positive value if \a str1 is greater than \a str2.

    If both strings are \nullptr, they are deemed equal; otherwise, if either is
    \nullptr, it is treated as less than the other (even if the other is an
    empty string).

    \sa qstrncmp(), qstricmp(), qstrnicmp(), {Character Case}, QByteArray::compare()
*/
int qstrcmp(const char *str1, const char *str2)
{
    return (str1 && str2) ? strcmp(str1, str2)
        : (str1 ? 1 : (str2 ? -1 : 0));
}

/*! \fn int qstrncmp(const char *str1, const char *str2, uint len);

    \relates QByteArray

    A safe \c strncmp() function.

    Compares at most \a len bytes of \a str1 and \a str2.

    Returns a negative value if \a str1 is less than \a str2, 0 if \a
    str1 is equal to \a str2 or a positive value if \a str1 is greater
    than \a str2.

    If both strings are \nullptr, they are deemed equal; otherwise, if either is
    \nullptr, it is treated as less than the other (even if the other is an
    empty string or \a len is 0).

    \sa qstrcmp(), qstricmp(), qstrnicmp(), {Character Case}, QByteArray::compare()
*/

/*! \relates QByteArray

    A safe \c stricmp() function.

    Compares \a str1 and \a str2, ignoring differences in the case of any ASCII
    characters.

    Returns a negative value if \a str1 is less than \a str2, 0 if \a
    str1 is equal to \a str2 or a positive value if \a str1 is greater
    than \a str2.

    If both strings are \nullptr, they are deemed equal; otherwise, if either is
    \nullptr, it is treated as less than the other (even if the other is an
    empty string).

    \sa qstrcmp(), qstrncmp(), qstrnicmp(), {Character Case}, QByteArray::compare()
*/

int qstricmp(const char *str1, const char *str2)
{
    const uchar *s1 = reinterpret_cast<const uchar *>(str1);
    const uchar *s2 = reinterpret_cast<const uchar *>(str2);
    if (!s1)
        return s2 ? -1 : 0;
    if (!s2)
        return 1;

    enum { Incomplete = 256 };
    qptrdiff offset = 0;
    auto innerCompare = [=, &offset](qptrdiff max, bool unlimited) {
        max += offset;
        do {
            uchar c = s1[offset];
            if (int res = asciiLower(c) - asciiLower(s2[offset]))
                return res;
            if (!c)
                return 0;
            ++offset;
        } while (unlimited || offset < max);
        return int(Incomplete);
    };

#if defined(__SSE4_1__) && !(defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer))
    enum { PageSize = 4096, PageMask = PageSize - 1 };
    const __m128i zero = _mm_setzero_si128();
    forever {
        // Calculate how many bytes we can load until we cross a page boundary
        // for either source. This isn't an exact calculation, just something
        // very quick.
        quintptr u1 = quintptr(s1 + offset);
        quintptr u2 = quintptr(s2 + offset);
        uint n = PageSize - ((u1 | u2) & PageMask);

        qptrdiff maxoffset = offset + n;
        for ( ; offset + 16 <= maxoffset; offset += sizeof(__m128i)) {
            // load 16 bytes from either source
            __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(s1 + offset));
            __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(s2 + offset));

            // compare the two against each oher
            __m128i cmp = _mm_cmpeq_epi8(a, b);

            // find NUL terminators too
            cmp = _mm_min_epu8(cmp, a);
            cmp = _mm_cmpeq_epi8(cmp, zero);

            // was there any difference or a NUL?
            uint mask = _mm_movemask_epi8(cmp);
            if (mask) {
                // yes, find out where
                uint start = qCountTrailingZeroBits(mask);
                uint end = sizeof(mask) * 8 - qCountLeadingZeroBits(mask);
                Q_ASSUME(end >= start);
                offset += start;
                n = end - start;
                break;
            }
        }

        // using SIMD could cause a page fault, so iterate byte by byte
        int res = innerCompare(n, false);
        if (res != Incomplete)
            return res;
    }
#endif

    return innerCompare(-1, true);
}

/*! \relates QByteArray

    A safe \c strnicmp() function.

    Compares at most \a len bytes of \a str1 and \a str2, ignoring differences
    in the case of any ASCII characters.

    Returns a negative value if \a str1 is less than \a str2, 0 if \a str1
    is equal to \a str2 or a positive value if \a str1 is greater than \a
    str2.

    If both strings are \nullptr, they are deemed equal; otherwise, if either is
    \nullptr, it is treated as less than the other (even if the other is an
    empty string or \a len is 0).

    \sa qstrcmp(), qstrncmp(), qstricmp(), {Character Case}, QByteArray::compare()
*/

int qstrnicmp(const char *str1, const char *str2, uint len)
{
    const uchar *s1 = reinterpret_cast<const uchar *>(str1);
    const uchar *s2 = reinterpret_cast<const uchar *>(str2);
    if (!s1 || !s2)
        return s1 ? 1 : (s2 ? -1 : 0);
    for (; len--; ++s1, ++s2) {
        const uchar c = *s1;
        if (int res = asciiLower(c) - asciiLower(*s2))
            return res;
        if (!c)                                // strings are equal
            break;
    }
    return 0;
}

/*!
    \internal
    \since 5.12

    A helper for QByteArray::compare. Compares \a len1 bytes from \a str1 to \a
    len2 bytes from \a str2. If \a len2 is -1, then \a str2 is expected to be
    '\\0'-terminated.
 */
int qstrnicmp(const char *str1, qsizetype len1, const char *str2, qsizetype len2)
{
    Q_ASSERT(len1 >= 0);
    Q_ASSERT(len2 >= -1);
    const uchar *s1 = reinterpret_cast<const uchar *>(str1);
    const uchar *s2 = reinterpret_cast<const uchar *>(str2);
    if (!s1 || !len1) {
        if (len2 == 0)
            return 0;
        if (len2 == -1)
            return (!s2 || !*s2) ? 0 : -1;
        Q_ASSERT(s2);
        return -1;
    }
    if (!s2)
        return len1 == 0 ? 0 : 1;

    if (len2 == -1) {
        // null-terminated str2
        qsizetype i;
        for (i = 0; i < len1; ++i) {
            const uchar c = s2[i];
            if (!c)
                return 1;

            if (int res = asciiLower(s1[i]) - asciiLower(c))
                return res;
        }
        return s2[i] ? -1 : 0;
    } else {
        // not null-terminated
        const qsizetype len = qMin(len1, len2);
        for (qsizetype i = 0; i < len; ++i) {
            if (int res = asciiLower(s1[i]) - asciiLower(s2[i]))
                return res;
        }
        if (len1 == len2)
            return 0;
        return len1 < len2 ? -1 : 1;
    }
}

/*!
    \internal
 */
int QtPrivate::compareMemory(QByteArrayView lhs, QByteArrayView rhs)
{
    if (!lhs.isNull() && !rhs.isNull()) {
        int ret = memcmp(lhs.data(), rhs.data(), qMin(lhs.size(), rhs.size()));
        if (ret != 0)
            return ret;
    }

    // they matched qMin(l1, l2) bytes
    // so the longer one is lexically after the shorter one
    return lhs.size() == rhs.size() ? 0 : lhs.size() > rhs.size() ? 1 : -1;
}

// the CRC table below is created by the following piece of code
#if 0
static void createCRC16Table()                        // build CRC16 lookup table
{
    unsigned int i;
    unsigned int j;
    unsigned short crc_tbl[16];
    unsigned int v0, v1, v2, v3;
    for (i = 0; i < 16; i++) {
        v0 = i & 1;
        v1 = (i >> 1) & 1;
        v2 = (i >> 2) & 1;
        v3 = (i >> 3) & 1;
        j = 0;
#undef SET_BIT
#define SET_BIT(x, b, v) (x) |= (v) << (b)
        SET_BIT(j,  0, v0);
        SET_BIT(j,  7, v0);
        SET_BIT(j, 12, v0);
        SET_BIT(j,  1, v1);
        SET_BIT(j,  8, v1);
        SET_BIT(j, 13, v1);
        SET_BIT(j,  2, v2);
        SET_BIT(j,  9, v2);
        SET_BIT(j, 14, v2);
        SET_BIT(j,  3, v3);
        SET_BIT(j, 10, v3);
        SET_BIT(j, 15, v3);
        crc_tbl[i] = j;
    }
    printf("static const quint16 crc_tbl[16] = {\n");
    for (int i = 0; i < 16; i +=4)
        printf("    0x%04x, 0x%04x, 0x%04x, 0x%04x,\n", crc_tbl[i], crc_tbl[i+1], crc_tbl[i+2], crc_tbl[i+3]);
    printf("};\n");
}
#endif

static const quint16 crc_tbl[16] = {
    0x0000, 0x1081, 0x2102, 0x3183,
    0x4204, 0x5285, 0x6306, 0x7387,
    0x8408, 0x9489, 0xa50a, 0xb58b,
    0xc60c, 0xd68d, 0xe70e, 0xf78f
};

/*!
    \relates QByteArray
    \since 5.9

    Returns the CRC-16 checksum of the first \a len bytes of \a data.

    The checksum is independent of the byte order (endianness) and will
    be calculated accorded to the algorithm published in \a standard.
    By default the algorithm published in ISO 3309 (Qt::ChecksumIso3309) is used.

    \note This function is a 16-bit cache conserving (16 entry table)
    implementation of the CRC-16-CCITT algorithm.
*/
quint16 qChecksum(const char *data, uint len, Qt::ChecksumType standard)
{
    quint16 crc = 0x0000;
    switch (standard) {
    case Qt::ChecksumIso3309:
        crc = 0xffff;
        break;
    case Qt::ChecksumItuV41:
        crc = 0x6363;
        break;
    }
    uchar c;
    const uchar *p = reinterpret_cast<const uchar *>(data);
    while (len--) {
        c = *p++;
        crc = ((crc >> 4) & 0x0fff) ^ crc_tbl[((crc ^ c) & 15)];
        c >>= 4;
        crc = ((crc >> 4) & 0x0fff) ^ crc_tbl[((crc ^ c) & 15)];
    }
    switch (standard) {
    case Qt::ChecksumIso3309:
        crc = ~crc;
        break;
    case Qt::ChecksumItuV41:
        break;
    }
    return crc & 0xffff;
}

/*!
    \fn QByteArray qCompress(const QByteArray& data, int compressionLevel)

    \relates QByteArray

    Compresses the \a data byte array and returns the compressed data
    in a new byte array.

    The \a compressionLevel parameter specifies how much compression
    should be used. Valid values are between 0 and 9, with 9
    corresponding to the greatest compression (i.e. smaller compressed
    data) at the cost of using a slower algorithm. Smaller values (8,
    7, ..., 1) provide successively less compression at slightly
    faster speeds. The value 0 corresponds to no compression at all.
    The default value is -1, which specifies zlib's default
    compression.

    \sa qUncompress()
*/

/*! \relates QByteArray

    \overload

    Compresses the first \a nbytes of \a data at compression level
    \a compressionLevel and returns the compressed data in a new byte array.
*/

#ifndef QT_NO_COMPRESS
QByteArray qCompress(const uchar* data, int nbytes, int compressionLevel)
{
    if (nbytes == 0) {
        return QByteArray(4, '\0');
    }
    if (!data) {
        qWarning("qCompress: Data is null");
        return QByteArray();
    }
    if (compressionLevel < -1 || compressionLevel > 9)
        compressionLevel = -1;

    ulong len = nbytes + nbytes / 100 + 13;
    QByteArray bazip;
    int res;
    do {
        bazip.resize(len + 4);
        res = ::compress2((uchar*)bazip.data()+4, &len, data, nbytes, compressionLevel);

        switch (res) {
        case Z_OK:
            bazip.resize(len + 4);
            bazip[0] = (nbytes & 0xff000000) >> 24;
            bazip[1] = (nbytes & 0x00ff0000) >> 16;
            bazip[2] = (nbytes & 0x0000ff00) >> 8;
            bazip[3] = (nbytes & 0x000000ff);
            break;
        case Z_MEM_ERROR:
            qWarning("qCompress: Z_MEM_ERROR: Not enough memory");
            bazip.resize(0);
            break;
        case Z_BUF_ERROR:
            len *= 2;
            break;
        }
    } while (res == Z_BUF_ERROR);

    return bazip;
}
#endif

/*!
    \fn QByteArray qUncompress(const QByteArray &data)

    \relates QByteArray

    Uncompresses the \a data byte array and returns a new byte array
    with the uncompressed data.

    Returns an empty QByteArray if the input data was corrupt.

    This function will uncompress data compressed with qCompress()
    from this and any earlier Qt version, back to Qt 3.1 when this
    feature was added.

    \b{Note:} If you want to use this function to uncompress external
    data that was compressed using zlib, you first need to prepend a four
    byte header to the byte array containing the data. The header must
    contain the expected length (in bytes) of the uncompressed data,
    expressed as an unsigned, big-endian, 32-bit integer.

    \sa qCompress()
*/

#ifndef QT_NO_COMPRESS
static QByteArray invalidCompressedData()
{
    qWarning("qUncompress: Input data is corrupted");
    return QByteArray();
}

/*! \relates QByteArray

    \overload

    Uncompresses the first \a nbytes of \a data and returns a new byte
    array with the uncompressed data.
*/
QByteArray qUncompress(const uchar* data, int nbytes)
{
    if (!data) {
        qWarning("qUncompress: Data is null");
        return QByteArray();
    }
    if (nbytes <= 4) {
        if (nbytes < 4 || (data[0]!=0 || data[1]!=0 || data[2]!=0 || data[3]!=0))
            qWarning("qUncompress: Input data is corrupted");
        return QByteArray();
    }
    ulong expectedSize = uint((data[0] << 24) | (data[1] << 16) |
                              (data[2] <<  8) | (data[3]      ));
    ulong len = qMax(expectedSize, 1ul);
    const ulong maxPossibleSize = MaxAllocSize - sizeof(QByteArray::Data);
    if (Q_UNLIKELY(len >= maxPossibleSize)) {
        // QByteArray does not support that huge size anyway.
        return invalidCompressedData();
    }

    QByteArray::DataPointer d(QByteArray::Data::allocate(expectedSize + 1));
    if (Q_UNLIKELY(d.data() == nullptr))
        return invalidCompressedData();

    forever {
        ulong alloc = len;
        int res = ::uncompress((uchar*)d.data(), &len,
                               data+4, nbytes-4);

        switch (res) {
        case Z_OK: {
            Q_ASSERT(len <= alloc);
            Q_UNUSED(alloc);
            d.data()[len] = '\0';
            d.size = len;
            return QByteArray(d);
        }

        case Z_MEM_ERROR:
            qWarning("qUncompress: Z_MEM_ERROR: Not enough memory");
            return QByteArray();

        case Z_BUF_ERROR:
            len *= 2;
            if (Q_UNLIKELY(len >= maxPossibleSize)) {
                // QByteArray does not support that huge size anyway.
                return invalidCompressedData();
            } else {
                // grow the block
                d->reallocate(d->allocatedCapacity()*2, QByteArray::Data::GrowsForward);
                if (Q_UNLIKELY(d.data() == nullptr))
                    return invalidCompressedData();
            }
            continue;

        case Z_DATA_ERROR:
            qWarning("qUncompress: Z_DATA_ERROR: Input data is corrupted");
            return QByteArray();
        }
    }
}
#endif

/*!
    \class QByteArray
    \inmodule QtCore
    \brief The QByteArray class provides an array of bytes.

    \ingroup tools
    \ingroup shared
    \ingroup string-processing

    \reentrant

    QByteArray can be used to store both raw bytes (including '\\0's)
    and traditional 8-bit '\\0'-terminated strings. Using QByteArray
    is much more convenient than using \c{const char *}. Behind the
    scenes, it always ensures that the data is followed by a '\\0'
    terminator, and uses \l{implicit sharing} (copy-on-write) to
    reduce memory usage and avoid needless copying of data.

    In addition to QByteArray, Qt also provides the QString class to store
    string data. For most purposes, QString is the class you want to use. It
    understands its content as Unicode text (encoded using UTF-16) where
    QByteArray aims to avoid assumptions about the encoding or semantics of the
    bytes it stores (aside from a few legacy cases where it uses ASCII).
    Furthermore, QString is used throughout in the Qt API. The two main cases
    where QByteArray is appropriate are when you need to store raw binary data,
    and when memory conservation is critical (e.g., with Qt for Embedded Linux).

    One way to initialize a QByteArray is simply to pass a \c{const
    char *} to its constructor. For example, the following code
    creates a byte array of size 5 containing the data "Hello":

    \snippet code/src_corelib_text_qbytearray.cpp 0

    Although the size() is 5, the byte array also maintains an extra '\\0' byte
    at the end so that if a function is used that asks for a pointer to the
    underlying data (e.g. a call to data()), the data pointed to is guaranteed
    to be '\\0'-terminated.

    QByteArray makes a deep copy of the \c{const char *} data, so you can modify
    it later without experiencing side effects. (If, for example for performance
    reasons, you don't want to take a deep copy of the data, use
    QByteArray::fromRawData() instead.)

    Another approach is to set the size of the array using resize() and to
    initialize the data byte by byte. QByteArray uses 0-based indexes, just like
    C++ arrays. To access the byte at a particular index position, you can use
    operator[](). On non-const byte arrays, operator[]() returns a reference to
    a byte that can be used on the left side of an assignment. For example:

    \snippet code/src_corelib_text_qbytearray.cpp 1

    For read-only access, an alternative syntax is to use at():

    \snippet code/src_corelib_text_qbytearray.cpp 2

    at() can be faster than operator[](), because it never causes a
    \l{deep copy} to occur.

    To extract many bytes at a time, use left(), right(), or mid().

    A QByteArray can embed '\\0' bytes. The size() function always
    returns the size of the whole array, including embedded '\\0'
    bytes, but excluding the terminating '\\0' added by QByteArray.
    For example:

    \snippet code/src_corelib_text_qbytearray.cpp 48

    If you want to obtain the length of the data up to and excluding the first
    '\\0' byte, call qstrlen() on the byte array.

    After a call to resize(), newly allocated bytes have undefined
    values. To set all the bytes to a particular value, call fill().

    To obtain a pointer to the actual bytes, call data() or constData(). These
    functions return a pointer to the beginning of the data. The pointer is
    guaranteed to remain valid until a non-const function is called on the
    QByteArray. It is also guaranteed that the data ends with a '\\0' byte
    unless the QByteArray was created from \l{fromRawData()}{raw data}. This
    '\\0' byte is automatically provided by QByteArray and is not counted in
    size().

    QByteArray provides the following basic functions for modifying
    the byte data: append(), prepend(), insert(), replace(), and
    remove(). For example:

    \snippet code/src_corelib_text_qbytearray.cpp 3

    The replace() and remove() functions' first two arguments are the
    position from which to start erasing and the number of bytes that
    should be erased.

    When you append() data to a non-empty array, the array will be
    reallocated and the new data copied to it. You can avoid this
    behavior by calling reserve(), which preallocates a certain amount
    of memory. You can also call capacity() to find out how much
    memory QByteArray actually allocated. Data appended to an empty
    array is not copied.

    If you want to find all occurrences of a particular byte or sequence of
    bytes in a QByteArray, use indexOf() or lastIndexOf(). The former searches
    forward starting from a given index position, the latter searches
    backward. Both return the index position of the byte sequence if they find
    it; otherwise, they return -1. For example, here's a typical loop that finds
    all occurrences of a particular string:

    \snippet code/src_corelib_text_qbytearray.cpp 4

    If you simply want to check whether a QByteArray contains a particular byte
    sequence, use contains(). If you want to find out how many times a
    particular byte sequence occurs in the byte array, use count(). If you want
    to replace all occurrences of a particular value with another, use one of
    the two-parameter replace() overloads.

    \l{QByteArray}s can be compared using overloaded operators such as
    operator<(), operator<=(), operator==(), operator>=(), and so on. The
    comparison is based exclusively on the numeric values of the bytes and is
    very fast, but is not what a human would
    expect. QString::localeAwareCompare() is a better choice for sorting
    user-interface strings.

    For historical reasons, QByteArray distinguishes between a null
    byte array and an empty byte array. A \e null byte array is a
    byte array that is initialized using QByteArray's default
    constructor or by passing (const char *)0 to the constructor. An
    \e empty byte array is any byte array with size 0. A null byte
    array is always empty, but an empty byte array isn't necessarily
    null:

    \snippet code/src_corelib_text_qbytearray.cpp 5

    All functions except isNull() treat null byte arrays the same as empty byte
    arrays. For example, data() returns a valid pointer (\e not nullptr) to a
    '\\0' byte for a null byte array and QByteArray() compares equal to
    QByteArray(""). We recommend that you always use isEmpty() and avoid
    isNull().

    \section1 Maximum size and out-of-memory conditions

    The current version of QByteArray is limited to just under 2 GB (2^31
    bytes) in size. The exact value is architecture-dependent, since it depends
    on the overhead required for managing the data block, but is no more than
    32 bytes. Raw data blocks are also limited by the use of \c int type in the
    current version to 2 GB minus 1 byte.

    In case memory allocation fails, QByteArray will throw a \c std::bad_alloc
    exception. Out of memory conditions in the Qt containers are the only case
    where Qt will throw exceptions.

    Note that the operating system may impose further limits on applications
    holding a lot of allocated memory, especially large, contiguous blocks.
    Such considerations, the configuration of such behavior or any mitigation
    are outside the scope of the QByteArray API.

    \section1 C locale and ASCII functions

    QByteArray generally handles data as bytes, without presuming any semantics;
    where it does presume semantics, it uses the C locale and ASCII encoding.
    Standard Unicode encodings are supported by QString, other encodings may be
    supported using QStringEncoder and QStringDecoder to convert to Unicode. For
    locale-specific interpretation of text, use QLocale or QString.

    \section2 C Strings

    Traditional C strings, also known as '\\0'-terminated strings, are sequences
    of bytes, specified by a start-point and implicitly including each byte up
    to, but not including, the first '\\0' byte thereafter. Methods that accept
    such a pointer, without a length, will interpret it as this sequence of
    bytes. Such a sequence, by construction, cannot contain a '\\0' byte.

    Take care when passing fixed size C arrays to QByteArray methods that accept
    a QByteArrayView: the length of the data on which the method will operate is
    determined by array size. A \c{char [N]} array will be handled as a view of
    size \c{N-1}, on the expectation that the array is a string literal with a '\\0'
    at index \c{N-1}. For example:

    \snippet code/src_corelib_text_qbytearray.cpp 54

    Other overloads accept a start-pointer and a byte-count; these use the given
    number of bytes, following the start address, regardless of whether any of
    them happen to be '\\0' bytes. In some cases, where there is no overload
    taking only a pointer, passing a length of -1 will cause the method to use
    the offset of the first '\\0' byte after the pointer as the length; a length
    of -1 should only be passed if the method explicitly says it does this (in
    which case it is typically a default argument).

    \section2 Spacing Characters

    A frequent requirement is to remove spacing characters from a byte array
    ('\\n', '\\t', ' ', etc.). If you want to remove spacing from both ends of a
    QByteArray, use trimmed(). If you want to also replace each run of spacing
    characters with a single space character within the byte array, use
    simplified(). Only ASCII spacing characters are recognized for these
    purposes.

    \section2 Number-String Conversions

    Functions that perform conversions between numeric data types and strings
    are performed in the C locale, regardless of the user's locale settings. Use
    QLocale to perform locale-aware conversions between numbers and strings.

    \section2 Character Case

    In QByteArray, the notion of uppercase and lowercase and of case-independent
    comparison is limited to ASCII. Non-ASCII characters are treated as
    caseless, since their case depends on encoding. This affects functions that
    support a case insensitive option or that change the case of their
    arguments. Functions that this affects include contains(), indexOf(),
    lastIndexOf(), isLower(), isUpper(), toLower() and toUpper().

    This issue does not apply to \l{QString}s since they represent characters
    using Unicode.

    \sa  QByteArrayView, QString, QBitArray
*/

/*!
    \enum QByteArray::Base64Option
    \since 5.2

    This enum contains the options available for encoding and decoding Base64.
    Base64 is defined by \l{RFC 4648}, with the following options:

    \value Base64Encoding     (default) The regular Base64 alphabet, called simply "base64"
    \value Base64UrlEncoding  An alternate alphabet, called "base64url", which replaces two
                              characters in the alphabet to be more friendly to URLs.
    \value KeepTrailingEquals (default) Keeps the trailing padding equal signs at the end
                              of the encoded data, so the data is always a size multiple of
                              four.
    \value OmitTrailingEquals Omits adding the padding equal signs at the end of the encoded
                              data.
    \value IgnoreBase64DecodingErrors  When decoding Base64-encoded data, ignores errors
                                       in the input; invalid characters are simply skipped.
                                       This enum value has been added in Qt 5.15.
    \value AbortOnBase64DecodingErrors When decoding Base64-encoded data, stops at the first
                                       decoding error.
                                       This enum value has been added in Qt 5.15.

    QByteArray::fromBase64Encoding() and QByteArray::fromBase64()
    ignore the KeepTrailingEquals and OmitTrailingEquals options. If
    the IgnoreBase64DecodingErrors option is specified, they will not
    flag errors in case trailing equal signs are missing or if there
    are too many of them. If instead the AbortOnBase64DecodingErrors is
    specified, then the input must either have no padding or have the
    correct amount of equal signs.
*/

/*! \fn QByteArray::iterator QByteArray::begin()

    Returns an \l{STL-style iterators}{STL-style iterator} pointing to the first
    byte in the byte-array.

    \sa constBegin(), end()
*/

/*! \fn QByteArray::const_iterator QByteArray::begin() const

    \overload begin()
*/

/*! \fn QByteArray::const_iterator QByteArray::cbegin() const
    \since 5.0

    Returns a const \l{STL-style iterators}{STL-style iterator} pointing to the
    first byte in the byte-array.

    \sa begin(), cend()
*/

/*! \fn QByteArray::const_iterator QByteArray::constBegin() const

    Returns a const \l{STL-style iterators}{STL-style iterator} pointing to the
    first byte in the byte-array.

    \sa begin(), constEnd()
*/

/*! \fn QByteArray::iterator QByteArray::end()

    Returns an \l{STL-style iterators}{STL-style iterator} pointing just after
    the last byte in the byte-array.

    \sa begin(), constEnd()
*/

/*! \fn QByteArray::const_iterator QByteArray::end() const

    \overload end()
*/

/*! \fn QByteArray::const_iterator QByteArray::cend() const
    \since 5.0

    Returns a const \l{STL-style iterators}{STL-style iterator} pointing just
    after the last byte in the byte-array.

    \sa cbegin(), end()
*/

/*! \fn QByteArray::const_iterator QByteArray::constEnd() const

    Returns a const \l{STL-style iterators}{STL-style iterator} pointing just
    after the last byte in the byte-array.

    \sa constBegin(), end()
*/

/*! \fn QByteArray::reverse_iterator QByteArray::rbegin()
    \since 5.6

    Returns a \l{STL-style iterators}{STL-style} reverse iterator pointing to the first
    byte in the byte-array, in reverse order.

    \sa begin(), crbegin(), rend()
*/

/*! \fn QByteArray::const_reverse_iterator QByteArray::rbegin() const
    \since 5.6
    \overload
*/

/*! \fn QByteArray::const_reverse_iterator QByteArray::crbegin() const
    \since 5.6

    Returns a const \l{STL-style iterators}{STL-style} reverse iterator pointing to the first
    byte in the byte-array, in reverse order.

    \sa begin(), rbegin(), rend()
*/

/*! \fn QByteArray::reverse_iterator QByteArray::rend()
    \since 5.6

    Returns a \l{STL-style iterators}{STL-style} reverse iterator pointing to one past
    the last byte in the byte-array, in reverse order.

    \sa end(), crend(), rbegin()
*/

/*! \fn QByteArray::const_reverse_iterator QByteArray::rend() const
    \since 5.6
    \overload
*/

/*! \fn QByteArray::const_reverse_iterator QByteArray::crend() const
    \since 5.6

    Returns a const \l{STL-style iterators}{STL-style} reverse iterator pointing to one
    past the last byte in the byte-array, in reverse order.

    \sa end(), rend(), rbegin()
*/

/*! \fn void QByteArray::push_back(const QByteArray &other)

    This function is provided for STL compatibility. It is equivalent
    to append(\a other).
*/

/*! \fn void QByteArray::push_back(const char *str)

    \overload

    Same as append(\a str).
*/

/*! \fn void QByteArray::push_back(char ch)

    \overload

    Same as append(\a ch).
*/

/*! \fn void QByteArray::push_front(const QByteArray &other)

    This function is provided for STL compatibility. It is equivalent
    to prepend(\a other).
*/

/*! \fn void QByteArray::push_front(const char *str)

    \overload

    Same as prepend(\a str).
*/

/*! \fn void QByteArray::push_front(char ch)

    \overload

    Same as prepend(\a ch).
*/

/*! \fn void QByteArray::shrink_to_fit()
    \since 5.10

    This function is provided for STL compatibility. It is equivalent to
    squeeze().
*/

/*! \fn QByteArray::QByteArray(const QByteArray &other)

    Constructs a copy of \a other.

    This operation takes \l{constant time}, because QByteArray is
    \l{implicitly shared}. This makes returning a QByteArray from a
    function very fast. If a shared instance is modified, it will be
    copied (copy-on-write), taking \l{linear time}.

    \sa operator=()
*/

/*!
    \fn QByteArray::QByteArray(QByteArray &&other)

    Move-constructs a QByteArray instance, making it point at the same
    object that \a other was pointing to.

    \since 5.2
*/

/*! \fn QByteArray::QByteArray(QByteArrayDataPtr dd)

    \internal

    Constructs a byte array pointing to the same data as \a dd.
*/

/*! \fn QByteArray::~QByteArray()
    Destroys the byte array.
*/

/*!
    Assigns \a other to this byte array and returns a reference to
    this byte array.
*/
QByteArray &QByteArray::operator=(const QByteArray & other) noexcept
{
    d = other.d;
    return *this;
}


/*!
    \overload

    Assigns \a str to this byte array.
*/

QByteArray &QByteArray::operator=(const char *str)
{
    if (!str) {
        d.clear();
    } else if (!*str) {
        d = DataPointer::fromRawData(&_empty, 0);
    } else {
        const int len = int(strlen(str));
        const uint fullLen = uint(len) + 1;
        if (d->needsDetach() || fullLen > d->allocatedCapacity()
                || (len < size() && fullLen < (d->allocatedCapacity() >> 1)))
            reallocData(fullLen, d->detachFlags());
        memcpy(d.data(), str, fullLen); // include null terminator
        d.size = len;
    }
    return *this;
}

/*!
    \fn QByteArray &QByteArray::operator=(QByteArray &&other)

    Move-assigns \a other to this QByteArray instance.

    \since 5.2
*/

/*! \fn void QByteArray::swap(QByteArray &other)
    \since 4.8

    Swaps byte array \a other with this byte array. This operation is very
    fast and never fails.
*/

/*! \fn int QByteArray::size() const

    Returns the number of bytes in this byte array.

    The last byte in the byte array is at position size() - 1. In addition,
    QByteArray ensures that the byte at position size() is always '\\0', so that
    you can use the return value of data() and constData() as arguments to
    functions that expect '\\0'-terminated strings. If the QByteArray object was
    created from a \l{fromRawData()}{raw data} that didn't include the trailing
    '\\0'-termination byte, then QByteArray doesn't add it automaticall unless a
    \l{deep copy} is created.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 6

    \sa isEmpty(), resize()
*/

/*! \fn bool QByteArray::isEmpty() const

    Returns \c true if the byte array has size 0; otherwise returns \c false.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 7

    \sa size()
*/

/*! \fn int QByteArray::capacity() const

    Returns the maximum number of bytes that can be stored in the
    byte array without forcing a reallocation.

    The sole purpose of this function is to provide a means of fine
    tuning QByteArray's memory usage. In general, you will rarely
    ever need to call this function. If you want to know how many
    bytes are in the byte array, call size().

    \note a statically allocated byte array will report a capacity of 0,
    even if it's not empty.

    \sa reserve(), squeeze()
*/

/*! \fn void QByteArray::reserve(int size)

    Attempts to allocate memory for at least \a size bytes. If you
    know in advance how large the byte array will be, you can call
    this function, and if you call resize() often you are likely to
    get better performance. If \a size is an underestimate, the worst
    that will happen is that the QByteArray will be a bit slower.

    The sole purpose of this function is to provide a means of fine
    tuning QByteArray's memory usage. In general, you will rarely
    ever need to call this function. If you want to change the size
    of the byte array, call resize().

    \sa squeeze(), capacity()
*/

/*! \fn void QByteArray::squeeze()

    Releases any memory not required to store the array's data.

    The sole purpose of this function is to provide a means of fine
    tuning QByteArray's memory usage. In general, you will rarely
    ever need to call this function.

    \sa reserve(), capacity()
*/

/*! \fn QByteArray::operator const char *() const
    \fn QByteArray::operator const void *() const

    \obsolete Use constData() instead.

    Returns a pointer to the data stored in the byte array. The
    pointer can be used to access the bytes that compose the array.
    The data is '\\0'-terminated. The pointer remains valid as long
    as the array isn't reallocated or destroyed.

    This operator is mostly useful to pass a byte array to a function
    that accepts a \c{const char *}.

    You can disable this operator by defining \c
    QT_NO_CAST_FROM_BYTEARRAY when you compile your applications.

    Note: A QByteArray can store any byte values including '\\0's,
    but most functions that take \c{char *} arguments assume that the
    data ends at the first '\\0' they encounter.

    \sa constData()
*/

/*!
  \macro QT_NO_CAST_FROM_BYTEARRAY
  \relates QByteArray

  Disables automatic conversions from QByteArray to
  const char * or const void *.

  \sa QT_NO_CAST_TO_ASCII, QT_NO_CAST_FROM_ASCII
*/

/*! \fn char *QByteArray::data()

    Returns a pointer to the data stored in the byte array. The pointer can be
    used to access and modify the bytes that compose the array. The data is
    '\\0'-terminated, i.e. the number of bytes you can access following the
    returned pointer is size() + 1, including the '\\0' terminator.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 8

    The pointer remains valid as long as the byte array isn't
    reallocated or destroyed. For read-only access, constData() is
    faster because it never causes a \l{deep copy} to occur.

    This function is mostly useful to pass a byte array to a function
    that accepts a \c{const char *}.

    The following example makes a copy of the char* returned by
    data(), but it will corrupt the heap and cause a crash because it
    does not allocate a byte for the '\\0' at the end:

    \snippet code/src_corelib_text_qbytearray.cpp 46

    This one allocates the correct amount of space:

    \snippet code/src_corelib_text_qbytearray.cpp 47

    Note: A QByteArray can store any byte values including '\\0's,
    but most functions that take \c{char *} arguments assume that the
    data ends at the first '\\0' they encounter.

    \sa constData(), operator[]()
*/

/*! \fn const char *QByteArray::data() const

    \overload
*/

/*! \fn const char *QByteArray::constData() const

    Returns a pointer to the data stored in the byte array. The pointer can be
    used to access the bytes that compose the array. The data is
    '\\0'-terminated unless the QByteArray object was created from raw data.
    The pointer remains valid as long as the byte array isn't reallocated or
    destroyed.

    This function is mostly useful to pass a byte array to a function
    that accepts a \c{const char *}.

    Note: A QByteArray can store any byte values including '\\0's,
    but most functions that take \c{char *} arguments assume that the
    data ends at the first '\\0' they encounter.

    \sa data(), operator[](), fromRawData()
*/

/*! \fn void QByteArray::detach()

    \internal
*/

/*! \fn bool QByteArray::isDetached() const

    \internal
*/

/*! \fn bool QByteArray::isSharedWith(const QByteArray &other) const

    \internal
*/

/*! \fn char QByteArray::at(int i) const

    Returns the byte at index position \a i in the byte array.

    \a i must be a valid index position in the byte array (i.e., 0 <=
    \a i < size()).

    \sa operator[]()
*/

/*! \fn char &QByteArray::operator[](int i)

    Returns the byte at index position \a i as a modifiable reference.

    \a i must be a valid index position in the byte array (i.e., 0 <=
    \a i < size()).

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 9

    \sa at()
*/

/*! \fn char QByteArray::operator[](int i) const

    \overload

    Same as at(\a i).
*/

/*!
    \fn char QByteArray::front() const
    \since 5.10

    Returns the first byte in the byte array.
    Same as \c{at(0)}.

    This function is provided for STL compatibility.

    \warning Calling this function on an empty byte array constitutes
    undefined behavior.

    \sa back(), at(), operator[]()
*/

/*!
    \fn char QByteArray::back() const
    \since 5.10

    Returns the last byte in the byte array.
    Same as \c{at(size() - 1)}.

    This function is provided for STL compatibility.

    \warning Calling this function on an empty byte array constitutes
    undefined behavior.

    \sa front(), at(), operator[]()
*/

/*!
    \fn char &QByteArray::front()
    \since 5.10

    Returns a reference to the first byte in the byte array.
    Same as \c{operator[](0)}.

    This function is provided for STL compatibility.

    \warning Calling this function on an empty byte array constitutes
    undefined behavior.

    \sa back(), at(), operator[]()
*/

/*!
    \fn char &QByteArray::back()
    \since 5.10

    Returns a reference to the last byte in the byte array.
    Same as \c{operator[](size() - 1)}.

    This function is provided for STL compatibility.

    \warning Calling this function on an empty byte array constitutes
    undefined behavior.

    \sa front(), at(), operator[]()
*/

/*! \fn bool QByteArray::contains(QByteArrayView bv) const
    \since 6.0

    Returns \c true if this byte array contains an occurrence of the
    sequence of bytes viewed by \a bv; otherwise returns \c false.

    \sa indexOf(), count()
*/

/*! \fn bool QByteArray::contains(char ch) const

    \overload

    Returns \c true if the byte array contains the byte \a ch;
    otherwise returns \c false.
*/

/*!

    Truncates the byte array at index position \a pos.

    If \a pos is beyond the end of the array, nothing happens.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 10

    \sa chop(), resize(), left()
*/
void QByteArray::truncate(int pos)
{
    if (pos < size())
        resize(pos);
}

/*!

    Removes \a n bytes from the end of the byte array.

    If \a n is greater than size(), the result is an empty byte
    array.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 11

    \sa truncate(), resize(), left()
*/

void QByteArray::chop(int n)
{
    if (n > 0)
        resize(size() - n);
}


/*! \fn QByteArray &QByteArray::operator+=(const QByteArray &ba)

    Appends the byte array \a ba onto the end of this byte array and
    returns a reference to this byte array.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 12

    Note: QByteArray is an \l{implicitly shared} class. Consequently,
    if you append to an empty byte array, then the byte array will just
    share the data held in \a ba. In this case, no copying of data is done,
    taking \l{constant time}. If a shared instance is modified, it will
    be copied (copy-on-write), taking \l{linear time}.

    If the byte array being appended to is not empty, a deep copy of the
    data is performed, taking \l{linear time}.

    This operation typically does not suffer from allocation overhead,
    because QByteArray preallocates extra space at the end of the data
    so that it may grow without reallocating for each append operation.

    \sa append(), prepend()
*/

/*! \fn QByteArray &QByteArray::operator+=(const char *str)

    \overload

    Appends the '\\0'-terminated string \a str onto the end of this byte array
    and returns a reference to this byte array.
*/

/*! \fn QByteArray &QByteArray::operator+=(char ch)

    \overload

    Appends the byte \a ch onto the end of this byte array and returns a
    reference to this byte array.
*/

/*! \fn int QByteArray::length() const

    Same as size().
*/

/*! \fn bool QByteArray::isNull() const

    Returns \c true if this byte array is null; otherwise returns \c false.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 13

    Qt makes a distinction between null byte arrays and empty byte
    arrays for historical reasons. For most applications, what
    matters is whether or not a byte array contains any data,
    and this can be determined using isEmpty().

    \sa isEmpty()
*/

/*! \fn QByteArray::QByteArray()

    Constructs an empty byte array.

    \sa isEmpty()
*/

/*!
    Constructs a byte array containing the first \a size bytes of
    array \a data.

    If \a data is 0, a null byte array is constructed.

    If \a size is negative, \a data is assumed to point to a '\\0'-terminated
    string and its length is determined dynamically.

    QByteArray makes a deep copy of the string data.

    \sa fromRawData()
*/

QByteArray::QByteArray(const char *data, int size)
{
    if (!data) {
        d = DataPointer();
    } else {
        if (size < 0)
            size = int(strlen(data));
        d = DataPointer(Data::allocate(uint(size) + 1u), size);
        memcpy(d.data(), data, size);
        d.data()[size] = '\0';
    }
}

/*!
    Constructs a byte array of size \a size with every byte set to \a ch.

    \sa fill()
*/

QByteArray::QByteArray(int size, char ch)
{
    if (size <= 0) {
        d = DataPointer::fromRawData(&_empty, 0);
    } else {
        d = DataPointer(Data::allocate(uint(size) + 1u), size);
        memset(d.data(), ch, size);
        d.data()[size] = '\0';
    }
}

/*!
    \internal

    Constructs a byte array of size \a size with uninitialized contents.
*/

QByteArray::QByteArray(int size, Qt::Initialization)
{
    d = DataPointer(Data::allocate(uint(size) + 1u), size);
    d.data()[size] = '\0';
}

/*!
    Sets the size of the byte array to \a size bytes.

    If \a size is greater than the current size, the byte array is
    extended to make it \a size bytes with the extra bytes added to
    the end. The new bytes are uninitialized.

    If \a size is less than the current size, bytes are removed from
    the end.

    \sa size(), truncate()
*/
void QByteArray::resize(int size)
{
    if (size < 0)
        size = 0;

    if (d->needsDetach() || size > capacity())
        reallocData(uint(size) + 1u, d->detachFlags() | Data::GrowsForward);
    d.size = size;
    if (d->allocatedCapacity())
        d.data()[size] = 0;
}

/*!
    Sets every byte in the byte array to \a ch. If \a size is different from -1
    (the default), the byte array is resized to size \a size beforehand.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 14

    \sa resize()
*/

QByteArray &QByteArray::fill(char ch, int size)
{
    resize(size < 0 ? this->size() : size);
    if (this->size())
        memset(d.data(), ch, this->size());
    return *this;
}

void QByteArray::reallocData(uint alloc, Data::ArrayOptions options)
{
    if (d->needsDetach()) {
        DataPointer dd(Data::allocate(alloc, options), qMin(qsizetype(alloc) - 1, d.size));
        if (dd.size > 0)
            ::memcpy(dd.data(), d.data(), dd.size);
        dd.data()[dd.size] = 0;
        d = dd;
    } else {
        d->reallocate(alloc, options);
    }
}

void QByteArray::expand(int i)
{
    resize(qMax(i + 1, size()));
}

/*!
   \internal
   Return a QByteArray that is sure to be '\\0'-terminated.

   By default, all QByteArray have an extra NUL at the end,
   guaranteeing that assumption. However, if QByteArray::fromRawData
   is used, then the NUL is there only if the user put it there. We
   can't be sure.
*/
QByteArray QByteArray::nulTerminated() const
{
    // is this fromRawData?
    if (d.isMutable())
        return *this;           // no, then we're sure we're zero terminated

    QByteArray copy(*this);
    copy.detach();
    return copy;
}

/*!
    Prepends the byte array \a ba to this byte array and returns a
    reference to this byte array.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 15

    This is the same as insert(0, \a ba).

    Note: QByteArray is an \l{implicitly shared} class. Consequently,
    if you prepend to an empty byte array, then the byte array will just
    share the data held in \a ba. In this case, no copying of data is done,
    taking \l{constant time}. If a shared instance is modified, it will
    be copied (copy-on-write), taking \l{linear time}.

    If the byte array being prepended to is not empty, a deep copy of the
    data is performed, taking \l{linear time}.

    \sa append(), insert()
*/

QByteArray &QByteArray::prepend(const QByteArray &ba)
{
    if (size() == 0 && ba.d.isMutable()) {
        *this = ba;
    } else if (ba.size() != 0) {
        QByteArray tmp = *this;
        *this = ba;
        append(tmp);
    }
    return *this;
}

/*!
    \overload

    Prepends the '\\0'-terminated string \a str to this byte array.
*/

QByteArray &QByteArray::prepend(const char *str)
{
    return prepend(str, qstrlen(str));
}

/*!
    \overload
    \since 4.6

    Prepends \a len bytes starting at \a str to this byte array.
    The bytes prepended may include '\\0' bytes.
*/

QByteArray &QByteArray::prepend(const char *str, int len)
{
    if (str) {
        if (d->needsDetach() || size() + len > capacity())
            reallocData(uint(size() + len) + 1u, d->detachFlags() | Data::GrowsForward);
        memmove(d.data()+len, d.data(), d.size);
        memcpy(d.data(), str, len);
        d.size += len;
        d.data()[d.size] = '\0';
    }
    return *this;
}

/*! \fn QByteArray &QByteArray::prepend(int count, char ch)

    \overload
    \since 5.7

    Prepends \a count copies of byte \a ch to this byte array.
*/

/*!
    \overload

    Prepends the byte \a ch to this byte array.
*/

QByteArray &QByteArray::prepend(char ch)
{
    if (d->needsDetach() || size() + 1 > capacity())
        reallocData(uint(size()) + 2u, d->detachFlags() | Data::GrowsForward);
    memmove(d.data()+1, d.data(), d.size);
    d.data()[0] = ch;
    ++d.size;
    d.data()[d.size] = '\0';
    return *this;
}

/*!
    Appends the byte array \a ba onto the end of this byte array.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 16

    This is the same as insert(size(), \a ba).

    Note: QByteArray is an \l{implicitly shared} class. Consequently,
    if you append to an empty byte array, then the byte array will just
    share the data held in \a ba. In this case, no copying of data is done,
    taking \l{constant time}. If a shared instance is modified, it will
    be copied (copy-on-write), taking \l{linear time}.

    If the byte array being appended to is not empty, a deep copy of the
    data is performed, taking \l{linear time}.

    This operation typically does not suffer from allocation overhead,
    because QByteArray preallocates extra space at the end of the data
    so that it may grow without reallocating for each append operation.

    \sa operator+=(), prepend(), insert()
*/

QByteArray &QByteArray::append(const QByteArray &ba)
{
    if (size() == 0 && ba.d.isMutable()) {
        *this = ba;
    } else if (ba.size() != 0) {
        if (d->needsDetach() || size() + ba.size() > capacity())
            reallocData(uint(size() + ba.size()) + 1u, d->detachFlags() | Data::GrowsForward);
        memcpy(d.data() + d.size, ba.data(), ba.size());
        d.size += ba.size();
        d.data()[d.size] = '\0';
    }
    return *this;
}

/*!
    \overload

    Appends the '\\0'-terminated string \a str to this byte array.
*/

QByteArray& QByteArray::append(const char *str)
{
    if (str) {
        const int len = int(strlen(str));
        if (d->needsDetach() || size() + len > capacity())
            reallocData(uint(size() + len) + 1u, d->detachFlags() | Data::GrowsForward);
        memcpy(d.data() + d.size, str, len + 1); // include null terminator
        d.size += len;
    }
    return *this;
}

/*!
    \overload append()

    Appends the first \a len bytes starting at \a str to this byte array and
    returns a reference to this byte array. The bytes appended may include '\\0'
    bytes.

    If \a len is negative, \a str will be assumed to be a '\\0'-terminated
    string and the length to be copied will be determined automatically using
    qstrlen().

    If \a len is zero or \a str is null, nothing is appended to the byte
    array. Ensure that \a len is \e not longer than \a str.
*/

QByteArray &QByteArray::append(const char *str, int len)
{
    if (len < 0)
        len = qstrlen(str);
    if (str && len) {
        if (d->needsDetach() || size() + len > capacity())
            reallocData(uint(size() + len) + 1u, d->detachFlags() | Data::GrowsForward);
        memcpy(d.data() + d.size, str, len);
        d.size += len;
        d.data()[d.size] = '\0';
    }
    return *this;
}

/*! \fn QByteArray &QByteArray::append(int count, char ch)

    \overload
    \since 5.7

    Appends \a count copies of byte \a ch to this byte array and returns a
    reference to this byte array.

    If \a count is negative or zero nothing is appended to the byte array.
*/

/*!
    \overload

    Appends the byte \a ch to this byte array.
*/

QByteArray& QByteArray::append(char ch)
{
    if (d->needsDetach() || size() + 1 > capacity())
        reallocData(uint(size()) + 2u, d->detachFlags() | Data::GrowsForward);
    d.data()[d.size++] = ch;
    d.data()[d.size] = '\0';
    return *this;
}

/*!
  \internal
  Inserts \a len bytes from the array \a arr at position \a pos and returns a
  reference the modified byte array.
*/
static inline QByteArray &qbytearray_insert(QByteArray *ba,
                                            int pos, const char *arr, int len)
{
    if (pos < 0 || len <= 0 || arr == nullptr)
        return *ba;

    int oldsize = ba->size();
    ba->resize(qMax(pos, oldsize) + len);
    char *dst = ba->data();
    if (pos > oldsize)
        ::memset(dst + oldsize, 0x20, pos - oldsize);
    else
        ::memmove(dst + pos + len, dst + pos, oldsize - pos);
    memcpy(dst + pos, arr, len);
    return *ba;
}

/*!
    Inserts the byte array \a ba at index position \a i and returns a
    reference to this byte array.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 17

    \sa append(), prepend(), replace(), remove()
*/

QByteArray &QByteArray::insert(int i, const QByteArray &ba)
{
    QByteArray copy(ba);
    return qbytearray_insert(this, i, copy.constData(), copy.size());
}

/*!
    \overload

    Inserts the '\\0'-terminated string \a str at position \a i in the byte
    array.

    If \a i is greater than size(), the array is first extended using
    resize().
*/

QByteArray &QByteArray::insert(int i, const char *str)
{
    return qbytearray_insert(this, i, str, qstrlen(str));
}

/*!
    \overload
    \since 4.6

    Inserts \a len bytes, starting at \a str, at position \a i in the byte
    array.

    If \a i is greater than size(), the array is first extended using
    resize().
*/

QByteArray &QByteArray::insert(int i, const char *str, int len)
{
    return qbytearray_insert(this, i, str, len);
}

/*!
    \overload

    Inserts byte \a ch at index position \a i in the byte array. If \a i is
    greater than size(), the array is first extended using resize().
*/

QByteArray &QByteArray::insert(int i, char ch)
{
    return qbytearray_insert(this, i, &ch, 1);
}

/*! \fn QByteArray &QByteArray::insert(int i, int count, char ch)

    \overload
    \since 5.7

    Inserts \a count copies of byte \a ch at index position \a i in the byte
    array.

    If \a i is greater than size(), the array is first extended using resize().
*/

QByteArray &QByteArray::insert(int i, int count, char ch)
{
    if (i < 0 || count <= 0)
        return *this;

    int oldsize = size();
    resize(qMax(i, oldsize) + count);
    char *dst = d.data();
    if (i > oldsize)
        ::memset(dst + oldsize, 0x20, i - oldsize);
    else if (i < oldsize)
        ::memmove(dst + i + count, dst + i, oldsize - i);
    ::memset(dst + i, ch, count);
    return *this;
}

/*!
    Removes \a len bytes from the array, starting at index position \a
    pos, and returns a reference to the array.

    If \a pos is out of range, nothing happens. If \a pos is valid,
    but \a pos + \a len is larger than the size of the array, the
    array is truncated at position \a pos.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 18

    \sa insert(), replace()
*/

QByteArray &QByteArray::remove(int pos, int len)
{
    if (len <= 0  || uint(pos) >= uint(size()))
        return *this;
    detach();
    if (len >= size() - pos) {
        resize(pos);
    } else {
        memmove(d.data() + pos, d.data() + pos + len, size() - pos - len);
        resize(size() - len);
    }
    return *this;
}

/*!
    Replaces \a len bytes from index position \a pos with the byte
    array \a after, and returns a reference to this byte array.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 19

    \sa insert(), remove()
*/

QByteArray &QByteArray::replace(int pos, int len, const QByteArray &after)
{
    if (len == after.size() && (pos + len <= size())) {
        detach();
        memmove(d.data() + pos, after.data(), len*sizeof(char));
        return *this;
    } else {
        QByteArray copy(after);
        // ### optimize me
        remove(pos, len);
        return insert(pos, copy);
    }
}

/*! \fn QByteArray &QByteArray::replace(int pos, int len, const char *after)

    \overload

    Replaces \a len bytes from index position \a pos with the
    '\\0'-terminated string \a after.

    Notice: this can change the length of the byte array.
*/
QByteArray &QByteArray::replace(int pos, int len, const char *after)
{
    return replace(pos,len,after,qstrlen(after));
}

/*! \fn QByteArray &QByteArray::replace(int pos, int len, const char *after, int alen)

    \overload

    Replaces \a len bytes from index position \a pos with \a alen bytes starting
    at position \a after. The bytes inserted may include '\\0' bytes.

    \since 4.7
*/
QByteArray &QByteArray::replace(int pos, int len, const char *after, int alen)
{
    if (len == alen && (pos + len <= size())) {
        detach();
        memcpy(d.data() + pos, after, len*sizeof(char));
        return *this;
    } else {
        remove(pos, len);
        return qbytearray_insert(this, pos, after, alen);
    }
}

// ### optimize all other replace method, by offering
// QByteArray::replace(const char *before, int blen, const char *after, int alen)

/*!
    \overload

    Replaces every occurrence of the byte array \a before with the
    byte array \a after.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 20
*/

QByteArray &QByteArray::replace(const QByteArray &before, const QByteArray &after)
{
    return replace(before.constData(), before.size(), after.constData(), after.size());
}

/*!
    \fn QByteArray &QByteArray::replace(const char *before, const QByteArray &after)
    \overload

    Replaces every occurrence of the '\\0'-terminated string \a before with the
    byte array \a after.
*/

QByteArray &QByteArray::replace(const char *c, const QByteArray &after)
{
    return replace(c, qstrlen(c), after.constData(), after.size());
}

/*!
    \fn QByteArray &QByteArray::replace(const char *before, int bsize, const char *after, int asize)
    \overload

    Replaces every occurrence of the \a bsize bytes starting at \a before with
    the \a size bytes starting at \a after. Since the sizes of the strings are
    given by \a bsize and \a asize, they may contain '\\0' bytes and do not need
    to be '\\0'-terminated.
*/

QByteArray &QByteArray::replace(const char *before, int bsize, const char *after, int asize)
{
    if (isNull() || (before == after && bsize == asize))
        return *this;

    // protect against before or after being part of this
    const char *a = after;
    const char *b = before;
    if (after >= constBegin() && after < constEnd()) {
        char *copy = (char *)malloc(asize);
        Q_CHECK_PTR(copy);
        memcpy(copy, after, asize);
        a = copy;
    }
    if (before >= constBegin() && before < constEnd()) {
        char *copy = (char *)malloc(bsize);
        Q_CHECK_PTR(copy);
        memcpy(copy, before, bsize);
        b = copy;
    }

    QByteArrayMatcher matcher(before, bsize);
    int index = 0;
    int len = size();
    char *d = data(); // detaches

    if (bsize == asize) {
        if (bsize) {
            while ((index = matcher.indexIn(*this, index)) != -1) {
                memcpy(d + index, a, asize);
                index += bsize;
            }
        }
    } else if (asize < bsize) {
        uint to = 0;
        uint movestart = 0;
        uint num = 0;
        while ((index = matcher.indexIn(*this, index)) != -1) {
            if (num) {
                int msize = index - movestart;
                if (msize > 0) {
                    memmove(d + to, d + movestart, msize);
                    to += msize;
                }
            } else {
                to = index;
            }
            if (asize) {
                memcpy(d + to, a, asize);
                to += asize;
            }
            index += bsize;
            movestart = index;
            num++;
        }
        if (num) {
            int msize = len - movestart;
            if (msize > 0)
                memmove(d + to, d + movestart, msize);
            resize(len - num*(bsize-asize));
        }
    } else {
        // the most complex case. We don't want to lose performance by doing repeated
        // copies and reallocs of the data.
        while (index != -1) {
            uint indices[4096];
            uint pos = 0;
            while(pos < 4095) {
                index = matcher.indexIn(*this, index);
                if (index == -1)
                    break;
                indices[pos++] = index;
                index += bsize;
                // avoid infinite loop
                if (!bsize)
                    index++;
            }
            if (!pos)
                break;

            // we have a table of replacement positions, use them for fast replacing
            int adjust = pos*(asize-bsize);
            // index has to be adjusted in case we get back into the loop above.
            if (index != -1)
                index += adjust;
            int newlen = len + adjust;
            int moveend = len;
            if (newlen > len) {
                resize(newlen);
                len = newlen;
            }
            d = this->d.data(); // data(), without the detach() check

            while(pos) {
                pos--;
                int movestart = indices[pos] + bsize;
                int insertstart = indices[pos] + pos*(asize-bsize);
                int moveto = insertstart + asize;
                memmove(d + moveto, d + movestart, (moveend - movestart));
                if (asize)
                    memcpy(d + insertstart, a, asize);
                moveend = movestart - bsize;
            }
        }
    }

    if (a != after)
        ::free(const_cast<char *>(a));
    if (b != before)
        ::free(const_cast<char *>(b));


    return *this;
}


/*!
    \fn QByteArray &QByteArray::replace(const QByteArray &before, const char *after)
    \overload

    Replaces every occurrence of the byte in \a before with the '\\0'-terminated
    string \a after.
*/

/*! \fn QByteArray &QByteArray::replace(const char *before, const char *after)

    \overload

    Replaces every occurrence of the '\\0'-terminated string \a before with the
    '\\0'-terminated string \a after.
*/

/*!
    \overload

    Replaces every occurrence of the byte \a before with the byte array \a
    after.
*/

QByteArray &QByteArray::replace(char before, const QByteArray &after)
{
    char b[2] = { before, '\0' };
    return replace(b, 1, after.constData(), after.size());
}

/*! \fn QByteArray &QByteArray::replace(char before, const char *after)

    \overload

    Replaces every occurrence of the byte \a before with the '\\0'-terminated
    string \a after.
*/

/*!
    \overload

    Replaces every occurrence of the byte \a before with the byte \a after.
*/

QByteArray &QByteArray::replace(char before, char after)
{
    if (!isEmpty()) {
        char *i = data();
        char *e = i + size();
        for (; i != e; ++i)
            if (*i == before)
                * i = after;
    }
    return *this;
}

/*!
    Splits the byte array into subarrays wherever \a sep occurs, and
    returns the list of those arrays. If \a sep does not match
    anywhere in the byte array, split() returns a single-element list
    containing this byte array.
*/

QList<QByteArray> QByteArray::split(char sep) const
{
    QList<QByteArray> list;
    int start = 0;
    int end;
    while ((end = indexOf(sep, start)) != -1) {
        list.append(mid(start, end - start));
        start = end + 1;
    }
    list.append(mid(start));
    return list;
}

/*!
    \since 4.5

    Returns a copy of this byte array repeated the specified number of \a times.

    If \a times is less than 1, an empty byte array is returned.

    Example:

    \snippet code/src_corelib_text_qbytearray.cpp 49
*/
QByteArray QByteArray::repeated(int times) const
{
    if (isEmpty())
        return *this;

    if (times <= 1) {
        if (times == 1)
            return *this;
        return QByteArray();
    }

    const int resultSize = times * size();

    QByteArray result;
    result.reserve(resultSize);
    if (result.capacity() != resultSize)
        return QByteArray(); // not enough memory

    memcpy(result.d.data(), data(), size());

    int sizeSoFar = size();
    char *end = result.d.data() + sizeSoFar;

    const int halfResultSize = resultSize >> 1;
    while (sizeSoFar <= halfResultSize) {
        memcpy(end, result.d.data(), sizeSoFar);
        end += sizeSoFar;
        sizeSoFar <<= 1;
    }
    memcpy(end, result.d.data(), resultSize - sizeSoFar);
    result.d.data()[resultSize] = '\0';
    result.d.size = resultSize;
    return result;
}

#define REHASH(a) \
    if (ol_minus_1 < sizeof(std::size_t) * CHAR_BIT) \
        hashHaystack -= (a) << ol_minus_1; \
    hashHaystack <<= 1

static inline qsizetype findCharHelper(QByteArrayView haystack, qsizetype from, char needle) noexcept
{
    if (from < 0)
        from = qMax(from + haystack.size(), qsizetype(0));
    if (from < haystack.size()) {
        const char *const b = haystack.data();
        if (const auto n = static_cast<const char *>(
                    memchr(b + from, needle, static_cast<size_t>(haystack.size() - from)))) {
            return n - b;
        }
    }
    return -1;
}

qsizetype QtPrivate::findByteArray(QByteArrayView haystack, qsizetype from, QByteArrayView needle) noexcept
{
    const auto ol = needle.size();
    if (ol == 0)
        return from;
    if (ol == 1)
        return findCharHelper(haystack, from, needle.front());

    const auto l = haystack.size();
    if (from > l || ol + from > l)
        return -1;

    return qFindByteArray(haystack.data(), haystack.size(), from, needle.data(), ol);
}

/*! \fn int QByteArray::indexOf(QByteArrayView bv, int from) const
    \since 6.0

    Returns the index position of the start of the first occurrence of the
    sequence of bytes viewed by \a bv in this byte array, searching forward
    from index position \a from. Returns -1 if no match is found.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 21

    \sa lastIndexOf(), contains(), count()
*/

/*!
    \overload

    Returns the index position of the start of the first occurrence of the
    byte \a ch in this byte array, searching forward from index position \a from.
    Returns -1 if no match is found.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 22

    \sa lastIndexOf(), contains()
*/

int QByteArray::indexOf(char ch, int from) const
{
    return static_cast<int>(findCharHelper(*this, from, ch));
}

static qsizetype lastIndexOfHelper(const char *haystack, qsizetype l, const char *needle,
                                   qsizetype ol, qsizetype from)
{
    auto delta = l - ol;
    if (from < 0)
        from = delta;
    if (from < 0 || from > l)
        return -1;
    if (from > delta)
        from = delta;

    const char *end = haystack;
    haystack += from;
    const auto ol_minus_1 = std::size_t(ol - 1);
    const char *n = needle + ol_minus_1;
    const char *h = haystack + ol_minus_1;
    std::size_t hashNeedle = 0, hashHaystack = 0;
    qsizetype idx;
    for (idx = 0; idx < ol; ++idx) {
        hashNeedle = ((hashNeedle<<1) + *(n-idx));
        hashHaystack = ((hashHaystack<<1) + *(h-idx));
    }
    hashHaystack -= *haystack;
    while (haystack >= end) {
        hashHaystack += *haystack;
        if (hashHaystack == hashNeedle && memcmp(needle, haystack, ol) == 0)
            return haystack - end;
        --haystack;
        REHASH(*(haystack + ol));
    }
    return -1;

}

static inline qsizetype lastIndexOfCharHelper(QByteArrayView haystack, qsizetype from, char needle) noexcept
{
    if (from < 0)
        from += haystack.size();
    else if (from > haystack.size())
        from = haystack.size() - 1;
    if (from >= 0) {
        const char *b = haystack.data();
        const char *n = b + from + 1;
        while (n-- != b) {
            if (*n == needle)
                return n - b;
        }
    }
    return -1;
}

qsizetype QtPrivate::lastIndexOf(QByteArrayView haystack, qsizetype from, QByteArrayView needle) noexcept
{
    if (haystack.isEmpty())
        return !needle.size() ? 0 : -1;
    const auto ol = needle.size();
    if (ol == 1)
        return lastIndexOfCharHelper(haystack, from, needle.front());

    return lastIndexOfHelper(haystack.data(), haystack.size(), needle.data(), ol, from);
}

/*! \fn int QByteArray::lastIndexOf(QByteArrayView bv, int from) const
    \since 6.0

    Returns the index position of the start of the last occurrence of the sequence
    of bytes viewed by \a bv in this byte array, searching backward from index
    position \a from. If \a from is -1 (the default), the search starts from the
    end of the byte array. Returns -1 if no match is found.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 23

    \sa indexOf(), contains(), count()
*/

/*!
    \overload

    Returns the index position of the start of the last occurrence of byte \a ch in
    this byte array, searching backward from index position \a from. If \a from is -1
    (the default), the search starts at the last byte (at index size() - 1). Returns
    -1 if no match is found.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 24

    \sa indexOf(), contains()
*/

int QByteArray::lastIndexOf(char ch, int from) const
{
    return static_cast<int>(lastIndexOfCharHelper(*this, from, ch));
}

static inline qsizetype countCharHelper(QByteArrayView haystack, char needle) noexcept
{
    qsizetype num = 0;
    for (char ch : haystack) {
        if (ch == needle)
            ++num;
    }
    return num;
}

qsizetype QtPrivate::count(QByteArrayView haystack, QByteArrayView needle) noexcept
{
    if (needle.size() == 1)
        return countCharHelper(haystack, needle[0]);

    qsizetype num = 0;
    qsizetype i = -1;
    if (haystack.size() > 500 && needle.size() > 5) {
        QByteArrayMatcher matcher(needle.data(), needle.size());
        while ((i = matcher.indexIn(haystack.data(), haystack.size(), i + 1)) != -1)
            ++num;
    } else {
        while ((i = haystack.indexOf(needle, i + 1)) != -1)
            ++num;
    }
    return num;
}

/*! \fn int QByteArray::count(const QByteArrayView &bv) const
    \since 6.0

    Returns the number of (potentially overlapping) occurrences of the
    sequence of bytes viewed by \a bv in this byte array.

    \sa contains(), indexOf()
*/

/*!
    \overload

    Returns the number of occurrences of byte \a ch in the byte array.

    \sa contains(), indexOf()
*/

int QByteArray::count(char ch) const
{
    return static_cast<int>(countCharHelper(*this, ch));
}

/*! \fn int QByteArray::count() const

    \overload

    Same as size().
*/

/*!
    \fn int QByteArray::compare(const QByteArrayView &bv, Qt::CaseSensitivity cs = Qt::CaseSensitive) const
    \since 6.0

    Returns an integer less than, equal to, or greater than zero depending on
    whether this QByteArray sorts before, at the same position as, or after the
    QByteArrayView \a bv. The comparison is performed according to case sensitivity
    \a cs.

    \sa operator==, {Character Case}
*/

bool QtPrivate::startsWith(QByteArrayView haystack, QByteArrayView needle) noexcept
{
    if (haystack.size() < needle.size())
        return false;
    if (haystack.data() == needle.data() || needle.size() == 0)
        return true;
    return memcmp(haystack.data(), needle.data(), needle.size()) == 0;
}

/*! \fn bool QByteArray::startsWith(QByteArrayView bv) const
    \since 6.0

    Returns \c true if this byte array starts with the sequence of bytes
    viewed by \a bv; otherwise returns \c false.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 25

    \sa endsWith(), left()
*/

/*!
    \fn bool QByteArray::startsWith(char ch) const
    \overload

    Returns \c true if this byte array starts with byte \a ch; otherwise returns
    \c false.
*/

bool QtPrivate::endsWith(QByteArrayView haystack, QByteArrayView needle) noexcept
{
    if (haystack.size() < needle.size())
        return false;
    if (haystack.end() == needle.end() || needle.size() == 0)
        return true;
    return memcmp(haystack.end() - needle.size(), needle.data(), needle.size()) == 0;
}

/*!
    \fn bool QByteArray::endsWith(QByteArrayView bv) const
    \since 6.0

    Returns \c true if this byte array ends with the sequence of bytes
    viewed by \a bv; otherwise returns \c false.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 26

    \sa startsWith(), right()
*/

/*!
    \fn bool QByteArray::endsWith(char ch) const
    \overload

    Returns \c true if this byte array ends with byte \a ch;
    otherwise returns \c false.
*/

/*
    Returns true if \a c is an uppercase ASCII letter.
 */
static constexpr inline bool isUpperCaseAscii(char c)
{
    return c >= 'A' && c <= 'Z';
}

/*!
    Returns \c true if this byte array contains only ASCII uppercase letters,
    otherwise returns \c false.
    \since 5.12

    \sa isLower(), toUpper()
*/
bool QByteArray::isUpper() const
{
    if (isEmpty())
        return false;

    const char *d = data();

    for (int i = 0, max = size(); i < max; ++i) {
        if (!isUpperCaseAscii(d[i]))
            return false;
    }

    return true;
}

/*
    Returns true if \a c is an lowercase ASCII letter.
 */
static constexpr inline bool isLowerCaseAscii(char c)
{
    return c >= 'a' && c <= 'z';
}

/*!
    Returns \c true if this byte array contains only lowercase ASCII letters,
    otherwise returns \c false.
    \since 5.12

    \sa isUpper(), toLower()
 */
bool QByteArray::isLower() const
{
    if (isEmpty())
        return false;

    const char *d = data();

    for (int i = 0, max = size(); i < max; ++i) {
        if (!isLowerCaseAscii(d[i]))
            return false;
    }

    return true;
}

/*!
    Returns a byte array that contains the first \a len bytes of this byte
    array.

    \obsolete Use first() instead in new code.

    The entire byte array is returned if \a len is greater than
    size().

    Returns an empty QByteArray if \a len is smaller than 0.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 27

    \sa first(), last(), startsWith(), chopped(), chop(), truncate()
*/

QByteArray QByteArray::left(int len)  const
{
    if (len >= size())
        return *this;
    if (len < 0)
        len = 0;
    return QByteArray(data(), len);
}

/*!
    Returns a byte array that contains the last \a len bytes of this byte array.

    \obsolete Use last() instead in new code.

    The entire byte array is returned if \a len is greater than
    size().

    Returns an empty QByteArray if \a len is smaller than 0.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 28

    \sa endsWith(), last(), first(), sliced(), chopped(), chop(), truncate()
*/
QByteArray QByteArray::right(int len) const
{
    if (len >= size())
        return *this;
    if (len < 0)
        len = 0;
    return QByteArray(end() - len, len);
}

/*!
    Returns a byte array containing \a len bytes from this byte array,
    starting at position \a pos.

    \obsolete Use sliced() instead in new code.

    If \a len is -1 (the default), or \a pos + \a len >= size(),
    returns a byte array containing all bytes starting at position \a
    pos until the end of the byte array.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 29

    \sa first(), last(), sliced(), chopped(), chop(), truncate()
*/

QByteArray QByteArray::mid(int pos, int len) const
{
    qsizetype p = pos;
    qsizetype l = len;
    using namespace QtPrivate;
    switch (QContainerImplHelper::mid(size(), &p, &l)) {
    case QContainerImplHelper::Null:
        return QByteArray();
    case QContainerImplHelper::Empty:
    {
        return QByteArray(DataPointer::fromRawData(&_empty, 0));
    }
    case QContainerImplHelper::Full:
        return *this;
    case QContainerImplHelper::Subset:
        return QByteArray(d.data() + p, l);
    }
    Q_UNREACHABLE();
    return QByteArray();
}

/*!
    \fn QByteArray QByteArray::first(qsizetype n) const
    \since 6.0

    Returns the first \a n bytes of the byte array.

    \note The behavior is undefined when \a n < 0 or \a n > size().

    \sa last(), sliced(), startsWith(), chopped(), chop(), truncate()
*/

/*!
    \fn QByteArray QByteArray::last(qsizetype n) const
    \since 6.0

    Returns the last \a n bytes of the byte array.

    \note The behavior is undefined when \a n < 0 or \a n > size().

    \sa first(), sliced(), endsWith(), chopped(), chop(), truncate()
*/

/*!
    \fn QByteArray QByteArray::sliced(qsizetype pos, qsizetype n) const
    \since 6.0

    Returns a byte array containing the \a n bytes of this object starting
    at position \a pos.

    \note The behavior is undefined when \a pos < 0, \a n < 0,
    or \a pos + \a n > size().

    \sa first(), last(), chopped(), chop(), truncate()
*/

/*!
    \fn QByteArray QByteArray::sliced(qsizetype pos) const
    \since 6.0
    \overload

    Returns a byte array containing the bytes starting at position \a pos
    in this object, and extending to the end of this object.

    \note The behavior is undefined when \a pos < 0 or \a pos > size().

    \sa first(), last(), sliced(), chopped(), chop(), truncate()
*/

/*!
    \fn QByteArray::chopped(int len) const
    \since 5.10

    Returns a byte array that contains the leftmost size() - \a len bytes of
    this byte array.

    \note The behavior is undefined if \a len is negative or greater than size().

    \sa endsWith(), left(), right(), mid(), chop(), truncate()
*/

/*!
    \fn QByteArray QByteArray::toLower() const

    Returns a copy of the byte array in which each ASCII uppercase letter
    converted to lowercase.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 30

    \sa isLower(), toUpper(), {Character Case}
*/

// prevent the compiler from inlining the function in each of
// toLower and toUpper when the only difference is the table being used
// (even with constant propagation, there's no gain in performance).
template <typename T>
Q_NEVER_INLINE
static QByteArray toCase_template(T &input, uchar (*lookup)(uchar))
{
    // find the first bad character in input
    const char *orig_begin = input.constBegin();
    const char *firstBad = orig_begin;
    const char *e = input.constEnd();
    for ( ; firstBad != e ; ++firstBad) {
        uchar ch = uchar(*firstBad);
        uchar converted = lookup(ch);
        if (ch != converted)
            break;
    }

    if (firstBad == e)
        return std::move(input);

    // transform the rest
    QByteArray s = std::move(input);    // will copy if T is const QByteArray
    char *b = s.begin();            // will detach if necessary
    char *p = b + (firstBad - orig_begin);
    e = b + s.size();
    for ( ; p != e; ++p)
        *p = char(lookup(uchar(*p)));
    return s;
}

QByteArray QByteArray::toLower_helper(const QByteArray &a)
{
    return toCase_template(a, asciiLower);
}

QByteArray QByteArray::toLower_helper(QByteArray &a)
{
    return toCase_template(a, asciiLower);
}

/*!
    \fn QByteArray QByteArray::toUpper() const

    Returns a copy of the byte array in which each ASCII lowercase letter
    converted to uppercase.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 31

    \sa isUpper(), toLower(), {Character Case}
*/

QByteArray QByteArray::toUpper_helper(const QByteArray &a)
{
    return toCase_template(a, asciiUpper);
}

QByteArray QByteArray::toUpper_helper(QByteArray &a)
{
    return toCase_template(a, asciiUpper);
}

/*! \fn void QByteArray::clear()

    Clears the contents of the byte array and makes it null.

    \sa resize(), isNull()
*/

void QByteArray::clear()
{
    d.clear();
}

#if !defined(QT_NO_DATASTREAM) || (defined(QT_BOOTSTRAPPED) && !defined(QT_BUILD_QMAKE))

/*! \relates QByteArray

    Writes byte array \a ba to the stream \a out and returns a reference
    to the stream.

    \sa {Serializing Qt Data Types}
*/

QDataStream &operator<<(QDataStream &out, const QByteArray &ba)
{
    if (ba.isNull() && out.version() >= 6) {
        out << (quint32)0xffffffff;
        return out;
    }
    return out.writeBytes(ba.constData(), ba.size());
}

/*! \relates QByteArray

    Reads a byte array into \a ba from the stream \a in and returns a
    reference to the stream.

    \sa {Serializing Qt Data Types}
*/

QDataStream &operator>>(QDataStream &in, QByteArray &ba)
{
    ba.clear();
    quint32 len;
    in >> len;
    if (len == 0xffffffff)
        return in;

    const quint32 Step = 1024 * 1024;
    quint32 allocated = 0;

    do {
        int blockSize = qMin(Step, len - allocated);
        ba.resize(allocated + blockSize);
        if (in.readRawData(ba.data() + allocated, blockSize) != blockSize) {
            ba.clear();
            in.setStatus(QDataStream::ReadPastEnd);
            return in;
        }
        allocated += blockSize;
    } while (allocated < len);

    return in;
}
#endif // QT_NO_DATASTREAM

/*! \fn bool QByteArray::operator==(const QString &str) const

    Returns \c true if this byte array is equal to the UTF-8 encoding of \a str;
    otherwise returns \c false.

    The comparison is case sensitive.

    You can disable this operator by defining \c
    QT_NO_CAST_FROM_ASCII when you compile your applications. You
    then need to call QString::fromUtf8(), QString::fromLatin1(),
    or QString::fromLocal8Bit() explicitly if you want to convert the byte
    array to a QString before doing the comparison.
*/

/*! \fn bool QByteArray::operator!=(const QString &str) const

    Returns \c true if this byte array is not equal to the UTF-8 encoding of \a
    str; otherwise returns \c false.

    The comparison is case sensitive.

    You can disable this operator by defining \c
    QT_NO_CAST_FROM_ASCII when you compile your applications. You
    then need to call QString::fromUtf8(), QString::fromLatin1(),
    or QString::fromLocal8Bit() explicitly if you want to convert the byte
    array to a QString before doing the comparison.
*/

/*! \fn bool QByteArray::operator<(const QString &str) const

    Returns \c true if this byte array is lexically less than the UTF-8 encoding
    of \a str; otherwise returns \c false.

    The comparison is case sensitive.

    You can disable this operator by defining \c
    QT_NO_CAST_FROM_ASCII when you compile your applications. You
    then need to call QString::fromUtf8(), QString::fromLatin1(),
    or QString::fromLocal8Bit() explicitly if you want to convert the byte
    array to a QString before doing the comparison.
*/

/*! \fn bool QByteArray::operator>(const QString &str) const

    Returns \c true if this byte array is lexically greater than the UTF-8
    encoding of \a str; otherwise returns \c false.

    The comparison is case sensitive.

    You can disable this operator by defining \c
    QT_NO_CAST_FROM_ASCII when you compile your applications. You
    then need to call QString::fromUtf8(), QString::fromLatin1(),
    or QString::fromLocal8Bit() explicitly if you want to convert the byte
    array to a QString before doing the comparison.
*/

/*! \fn bool QByteArray::operator<=(const QString &str) const

    Returns \c true if this byte array is lexically less than or equal to the
    UTF-8 encoding of \a str; otherwise returns \c false.

    The comparison is case sensitive.

    You can disable this operator by defining \c
    QT_NO_CAST_FROM_ASCII when you compile your applications. You
    then need to call QString::fromUtf8(), QString::fromLatin1(),
    or QString::fromLocal8Bit() explicitly if you want to convert the byte
    array to a QString before doing the comparison.
*/

/*! \fn bool QByteArray::operator>=(const QString &str) const

    Returns \c true if this byte array is greater than or equal to the UTF-8
    encoding of \a str; otherwise returns \c false.

    The comparison is case sensitive.

    You can disable this operator by defining \c
    QT_NO_CAST_FROM_ASCII when you compile your applications. You
    then need to call QString::fromUtf8(), QString::fromLatin1(),
    or QString::fromLocal8Bit() explicitly if you want to convert the byte
    array to a QString before doing the comparison.
*/

/*! \fn bool operator==(const QByteArray &a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is equal to byte array \a a2;
    otherwise returns \c false.

    \sa QByteArray::compare()
*/
bool operator==(const QByteArray &a1, const QByteArray &a2) noexcept
{
    return (a1.size() == a2.size()) && (memcmp(a1.constData(), a2.constData(), a1.size())==0);
}

/*! \fn bool operator==(const QByteArray &a1, const char *a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is equal to the '\\0'-terminated string
    \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator==(const char *a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if '\\0'-terminated string \a a1 is equal to byte array \a
    a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator!=(const QByteArray &a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is not equal to byte array \a a2;
    otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator!=(const QByteArray &a1, const char *a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is not equal to the '\\0'-terminated
    string \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator!=(const char *a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if '\\0'-terminated string \a a1 is not equal to byte array
    \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator<(const QByteArray &a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is lexically less than byte array
    \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn inline bool operator<(const QByteArray &a1, const char *a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is lexically less than the
    '\\0'-terminated string \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator<(const char *a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if '\\0'-terminated string \a a1 is lexically less than byte
    array \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator<=(const QByteArray &a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is lexically less than or equal
    to byte array \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator<=(const QByteArray &a1, const char *a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is lexically less than or equal to the
    '\\0'-terminated string \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator<=(const char *a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if '\\0'-terminated string \a a1 is lexically less than or
    equal to byte array \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator>(const QByteArray &a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is lexically greater than byte
    array \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator>(const QByteArray &a1, const char *a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is lexically greater than the
    '\\0'-terminated string \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator>(const char *a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if '\\0'-terminated string \a a1 is lexically greater than
    byte array \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator>=(const QByteArray &a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is lexically greater than or
    equal to byte array \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator>=(const QByteArray &a1, const char *a2)
    \relates QByteArray

    \overload

    Returns \c true if byte array \a a1 is lexically greater than or equal to
    the '\\0'-terminated string \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn bool operator>=(const char *a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns \c true if '\\0'-terminated string \a a1 is lexically greater than
    or equal to byte array \a a2; otherwise returns \c false.

    \sa QByteArray::compare()
*/

/*! \fn const QByteArray operator+(const QByteArray &a1, const QByteArray &a2)
    \relates QByteArray

    Returns a byte array that is the result of concatenating byte
    array \a a1 and byte array \a a2.

    \sa QByteArray::operator+=()
*/

/*! \fn const QByteArray operator+(const QByteArray &a1, const char *a2)
    \relates QByteArray

    \overload

    Returns a byte array that is the result of concatenating byte array \a a1
    and '\\0'-terminated string \a a2.
*/

/*! \fn const QByteArray operator+(const QByteArray &a1, char a2)
    \relates QByteArray

    \overload

    Returns a byte array that is the result of concatenating byte
    array \a a1 and byte \a a2.
*/

/*! \fn const QByteArray operator+(const char *a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns a byte array that is the result of concatenating '\\0'-terminated
    string \a a1 and byte array \a a2.
*/

/*! \fn const QByteArray operator+(char a1, const QByteArray &a2)
    \relates QByteArray

    \overload

    Returns a byte array that is the result of concatenating byte \a a1 and byte
    array \a a2.
*/

/*!
    \fn QByteArray QByteArray::simplified() const

    Returns a copy of this byte array that has spacing characters removed from
    the start and end, and in which each sequence of internal spacing characters
    is replaced with a single space.

    The spacing characters are those for which the standard C++ \c isspace()
    function returns \c true in the C locale; these are the ASCII characters
    tabulation '\\t', line feed '\\n', carriage return '\\r', vertical
    tabulation '\\v', form feed '\\f', and space ' '.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 32

    \sa trimmed(), QChar::SpecialCharacter, {Spacing Characters}
*/
QByteArray QByteArray::simplified_helper(const QByteArray &a)
{
    return QStringAlgorithms<const QByteArray>::simplified_helper(a);
}

QByteArray QByteArray::simplified_helper(QByteArray &a)
{
    return QStringAlgorithms<QByteArray>::simplified_helper(a);
}

/*!
    \fn QByteArray QByteArray::trimmed() const

    Returns a copy of this byte array with spacing characters removed from the
    start and end.

    The spacing characters are those for which the standard C++ \c isspace()
    function returns \c true in the C locale; these are the ASCII characters
    tabulation '\\t', line feed '\\n', carriage return '\\r', vertical
    tabulation '\\v', form feed '\\f', and space ' '.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 33

    Unlike simplified(), \l {QByteArray::trimmed()}{trimmed()} leaves internal
    spacing unchanged.

    \sa simplified(), QChar::SpecialCharacter, {Spacing Characters}
*/
QByteArray QByteArray::trimmed_helper(const QByteArray &a)
{
    return QStringAlgorithms<const QByteArray>::trimmed_helper(a);
}

QByteArray QByteArray::trimmed_helper(QByteArray &a)
{
    return QStringAlgorithms<QByteArray>::trimmed_helper(a);
}


/*!
    Returns a byte array of size \a width that contains this byte array padded
    with the \a fill byte.

    If \a truncate is false and the size() of the byte array is more
    than \a width, then the returned byte array is a copy of this byte
    array.

    If \a truncate is true and the size() of the byte array is more
    than \a width, then any bytes in a copy of the byte array
    after position \a width are removed, and the copy is returned.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 34

    \sa rightJustified()
*/

QByteArray QByteArray::leftJustified(int width, char fill, bool truncate) const
{
    QByteArray result;
    int len = size();
    int padlen = width - len;
    if (padlen > 0) {
        result.resize(len+padlen);
        if (len)
            memcpy(result.d.data(), data(), len);
        memset(result.d.data()+len, fill, padlen);
    } else {
        if (truncate)
            result = left(width);
        else
            result = *this;
    }
    return result;
}

/*!
    Returns a byte array of size \a width that contains the \a fill byte
    followed by this byte array.

    If \a truncate is false and the size of the byte array is more
    than \a width, then the returned byte array is a copy of this byte
    array.

    If \a truncate is true and the size of the byte array is more
    than \a width, then the resulting byte array is truncated at
    position \a width.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 35

    \sa leftJustified()
*/

QByteArray QByteArray::rightJustified(int width, char fill, bool truncate) const
{
    QByteArray result;
    int len = size();
    int padlen = width - len;
    if (padlen > 0) {
        result.resize(len+padlen);
        if (len)
            memcpy(result.d.data()+padlen, data(), len);
        memset(result.d.data(), fill, padlen);
    } else {
        if (truncate)
            result = left(width);
        else
            result = *this;
    }
    return result;
}

bool QByteArray::isNull() const
{
    return d->isNull();
}

static qlonglong toIntegral_helper(const char *data, bool *ok, int base, qlonglong)
{
    return QLocaleData::bytearrayToLongLong(data, base, ok);
}

static qulonglong toIntegral_helper(const char *data, bool *ok, int base, qulonglong)
{
    return QLocaleData::bytearrayToUnsLongLong(data, base, ok);
}

template <typename T> static inline
T toIntegral_helper(const char *data, bool *ok, int base)
{
    using Int64 = typename std::conditional<std::is_unsigned<T>::value, qulonglong, qlonglong>::type;

#if defined(QT_CHECK_RANGE)
    if (base != 0 && (base < 2 || base > 36)) {
        qWarning("QByteArray::toIntegral: Invalid base %d", base);
        base = 10;
    }
#endif
    if (!data) {
        if (ok)
            *ok = false;
        return 0;
    }

    // we select the right overload by the last, unused parameter
    Int64 val = toIntegral_helper(data, ok, base, Int64());
    if (T(val) != val) {
        if (ok)
            *ok = false;
        val = 0;
    }
    return T(val);
}

/*!
    Returns the byte array converted to a \c {long long} using base \a base,
    which is ten by default. Bases 0 and 2 through 36 are supported, using
    letters for digits beyond 9; A is ten, B is eleven and so on.

    If \a base is 0, the base is determined automatically using the following
    rules: If the byte array begins with "0x", it is assumed to be hexadecimal
    (base 16); otherwise, if it begins with "0", it is assumed to be octal (base
    8); otherwise it is assumed to be decimal.

    Returns 0 if the conversion fails.

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number()
*/

qlonglong QByteArray::toLongLong(bool *ok, int base) const
{
    return toIntegral_helper<qlonglong>(nulTerminated().constData(), ok, base);
}

/*!
    Returns the byte array converted to an \c {unsigned long long} using base \a
    base, which is ten by default. Bases 0 and 2 through 36 are supported, using
    letters for digits beyond 9; A is ten, B is eleven and so on.

    If \a base is 0, the base is determined automatically using the following
    rules: If the byte array begins with "0x", it is assumed to be hexadecimal
    (base 16); otherwise, if it begins with "0", it is assumed to be octal (base
    8); otherwise it is assumed to be decimal.

    Returns 0 if the conversion fails.

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number()
*/

qulonglong QByteArray::toULongLong(bool *ok, int base) const
{
    return toIntegral_helper<qulonglong>(nulTerminated().constData(), ok, base);
}

/*!
    Returns the byte array converted to an \c int using base \a base, which is
    ten by default. Bases 0 and 2 through 36 are supported, using letters for
    digits beyond 9; A is ten, B is eleven and so on.

    If \a base is 0, the base is determined automatically using the following
    rules: If the byte array begins with "0x", it is assumed to be hexadecimal
    (base 16); otherwise, if it begins with "0", it is assumed to be octal (base
    8); otherwise it is assumed to be decimal.

    Returns 0 if the conversion fails.

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \snippet code/src_corelib_text_qbytearray.cpp 36

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number()
*/

int QByteArray::toInt(bool *ok, int base) const
{
    return toIntegral_helper<int>(nulTerminated().constData(), ok, base);
}

/*!
    Returns the byte array converted to an \c {unsigned int} using base \a base,
    which is ten by default. Bases 0 and 2 through 36 are supported, using
    letters for digits beyond 9; A is ten, B is eleven and so on.

    If \a base is 0, the base is determined automatically using the following
    rules: If the byte array begins with "0x", it is assumed to be hexadecimal
    (base 16); otherwise, if it begins with "0", it is assumed to be octal (base
    8); otherwise it is assumed to be decimal.

    Returns 0 if the conversion fails.

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number()
*/

uint QByteArray::toUInt(bool *ok, int base) const
{
    return toIntegral_helper<uint>(nulTerminated().constData(), ok, base);
}

/*!
    \since 4.1

    Returns the byte array converted to a \c long int using base \a base, which
    is ten by default. Bases 0 and 2 through 36 are supported, using letters for
    digits beyond 9; A is ten, B is eleven and so on.

    If \a base is 0, the base is determined automatically using the following
    rules: If the byte array begins with "0x", it is assumed to be hexadecimal
    (base 16); otherwise, if it begins with "0", it is assumed to be octal (base
    8); otherwise it is assumed to be decimal.

    Returns 0 if the conversion fails.

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \snippet code/src_corelib_text_qbytearray.cpp 37

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number()
*/
long QByteArray::toLong(bool *ok, int base) const
{
    return toIntegral_helper<long>(nulTerminated().constData(), ok, base);
}

/*!
    \since 4.1

    Returns the byte array converted to an \c {unsigned long int} using base \a
    base, which is ten by default. Bases 0 and 2 through 36 are supported, using
    letters for digits beyond 9; A is ten, B is eleven and so on.

    If \a base is 0, the base is determined automatically using the following
    rules: If the byte array begins with "0x", it is assumed to be hexadecimal
    (base 16); otherwise, if it begins with "0", it is assumed to be octal (base
    8); otherwise it is assumed to be decimal.

    Returns 0 if the conversion fails.

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number()
*/
ulong QByteArray::toULong(bool *ok, int base) const
{
    return toIntegral_helper<ulong>(nulTerminated().constData(), ok, base);
}

/*!
    Returns the byte array converted to a \c short using base \a base, which is
    ten by default. Bases 0 and 2 through 36 are supported, using letters for
    digits beyond 9; A is ten, B is eleven and so on.

    If \a base is 0, the base is determined automatically using the following
    rules: If the byte array begins with "0x", it is assumed to be hexadecimal;
    otherwise, if it begins with "0", it is assumed to be octal; otherwise it is
    assumed to be decimal.

    Returns 0 if the conversion fails.

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number()
*/

short QByteArray::toShort(bool *ok, int base) const
{
    return toIntegral_helper<short>(nulTerminated().constData(), ok, base);
}

/*!
    Returns the byte array converted to an \c {unsigned short} using base \a
    base, which is ten by default. Bases 0 and 2 through 36 are supported, using
    letters for digits beyond 9; A is ten, B is eleven and so on.

    If \a base is 0, the base is determined automatically using the following
    rules: If the byte array begins with "0x", it is assumed to be hexadecimal;
    otherwise, if it begins with "0", it is assumed to be octal; otherwise it is
    assumed to be decimal.

    Returns 0 if the conversion fails.

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number()
*/

ushort QByteArray::toUShort(bool *ok, int base) const
{
    return toIntegral_helper<ushort>(nulTerminated().constData(), ok, base);
}


/*!
    Returns the byte array converted to a \c double value.

    Returns an infinity if the conversion overflows or 0.0 if the
    conversion fails for other reasons (e.g. underflow).

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \snippet code/src_corelib_text_qbytearray.cpp 38

    \warning The QByteArray content may only contain valid numerical characters
    which includes the plus/minus sign, the character e used in scientific
    notation, and the decimal point. Including the unit or additional characters
    leads to a conversion error.

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    This function ignores leading and trailing whitespace.

    \sa number()
*/

double QByteArray::toDouble(bool *ok) const
{
    bool nonNullOk = false;
    int processed = 0;
    double d = qt_asciiToDouble(constData(), size(),
                                nonNullOk, processed, WhitespacesAllowed);
    if (ok)
        *ok = nonNullOk;
    return d;
}

/*!
    Returns the byte array converted to a \c float value.

    Returns an infinity if the conversion overflows or 0.0 if the
    conversion fails for other reasons (e.g. underflow).

    If \a ok is not \nullptr, failure is reported by setting *\a{ok}
    to \c false, and success by setting *\a{ok} to \c true.

    \snippet code/src_corelib_text_qbytearray.cpp 38float

    \warning The QByteArray content may only contain valid numerical characters
    which includes the plus/minus sign, the character e used in scientific
    notation, and the decimal point. Including the unit or additional characters
    leads to a conversion error.

    \note The conversion of the number is performed in the default C locale,
    regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    This function ignores leading and trailing whitespace.

    \sa number()
*/

float QByteArray::toFloat(bool *ok) const
{
    return QLocaleData::convertDoubleToFloat(toDouble(ok), ok);
}

/*!
    \since 5.2

    Returns a copy of the byte array, encoded using the options \a options.

    \snippet code/src_corelib_text_qbytearray.cpp 39

    The algorithm used to encode Base64-encoded data is defined in \l{RFC 4648}.

    \sa fromBase64()
*/
QByteArray QByteArray::toBase64(Base64Options options) const
{
    const char alphabet_base64[] = "ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZabcdef"
                                   "ghijklmn" "opqrstuv" "wxyz0123" "456789+/";
    const char alphabet_base64url[] = "ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZabcdef"
                                      "ghijklmn" "opqrstuv" "wxyz0123" "456789-_";
    const char *const alphabet = options & Base64UrlEncoding ? alphabet_base64url : alphabet_base64;
    const char padchar = '=';
    int padlen = 0;

    QByteArray tmp((size() + 2) / 3 * 4, Qt::Uninitialized);

    int i = 0;
    char *out = tmp.data();
    while (i < size()) {
        // encode 3 bytes at a time
        int chunk = 0;
        chunk |= int(uchar(data()[i++])) << 16;
        if (i == size()) {
            padlen = 2;
        } else {
            chunk |= int(uchar(data()[i++])) << 8;
            if (i == size())
                padlen = 1;
            else
                chunk |= int(uchar(data()[i++]));
        }

        int j = (chunk & 0x00fc0000) >> 18;
        int k = (chunk & 0x0003f000) >> 12;
        int l = (chunk & 0x00000fc0) >> 6;
        int m = (chunk & 0x0000003f);
        *out++ = alphabet[j];
        *out++ = alphabet[k];

        if (padlen > 1) {
            if ((options & OmitTrailingEquals) == 0)
                *out++ = padchar;
        } else {
            *out++ = alphabet[l];
        }
        if (padlen > 0) {
            if ((options & OmitTrailingEquals) == 0)
                *out++ = padchar;
        } else {
            *out++ = alphabet[m];
        }
    }
    Q_ASSERT((options & OmitTrailingEquals) || (out == tmp.size() + tmp.data()));
    if (options & OmitTrailingEquals)
        tmp.truncate(out - tmp.data());
    return tmp;
}

/*!
    \fn QByteArray &QByteArray::setNum(int n, int base)

    Sets the byte array to the printed value of \a n in base \a base (ten by
    default) and returns a reference to the byte array. Bases 2 through 36 are
    supported, using letters for digits beyond 9; A is ten, B is eleven and so
    on. For bases other than ten, n is treated as an unsigned integer.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 40

    \note The format of the number is not localized; the default C locale is
    used regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa number(), toInt()
*/

/*!
    \fn QByteArray &QByteArray::setNum(uint n, int base)
    \overload

    \sa toUInt()
*/

/*!
    \fn QByteArray &QByteArray::setNum(long n, int base)
    \overload

    \sa toLong()
*/

/*!
    \fn QByteArray &QByteArray::setNum(ulong n, int base)
    \overload

    \sa toULong()
*/

/*!
    \fn QByteArray &QByteArray::setNum(short n, int base)
    \overload

    \sa toShort()
*/

/*!
    \fn QByteArray &QByteArray::setNum(ushort n, int base)
    \overload

    \sa toUShort()
*/

static char *qulltoa2(char *p, qulonglong n, int base)
{
#if defined(QT_CHECK_RANGE)
    if (base < 2 || base > 36) {
        qWarning("QByteArray::setNum: Invalid base %d", base);
        base = 10;
    }
#endif
    const char b = 'a' - 10;
    do {
        const int c = n % base;
        n /= base;
        *--p = c + (c < 10 ? '0' : b);
    } while (n);

    return p;
}

/*!
    \overload

    \sa toLongLong()
*/
QByteArray &QByteArray::setNum(qlonglong n, int base)
{
    const int buffsize = 66; // big enough for MAX_ULLONG in base 2
    char buff[buffsize];
    char *p;

    if (n < 0 && base == 10) {
        p = qulltoa2(buff + buffsize, qulonglong(-(1 + n)) + 1, base);
        *--p = '-';
    } else {
        p = qulltoa2(buff + buffsize, qulonglong(n), base);
    }

    clear();
    append(p, buffsize - (p - buff));
    return *this;
}

/*!
    \overload

    \sa toULongLong()
*/

QByteArray &QByteArray::setNum(qulonglong n, int base)
{
    const int buffsize = 66; // big enough for MAX_ULLONG in base 2
    char buff[buffsize];
    char *p = qulltoa2(buff + buffsize, n, base);

    clear();
    append(p, buffsize - (p - buff));
    return *this;
}

/*!
    \overload

    Sets the byte array to the printed value of \a n, formatted in format
    \a f with precision \a prec, and returns a reference to the
    byte array.

    The format \a f can be any of the following:

    \table
    \header \li Format \li Meaning
    \row \li \c e \li format as [-]9.9e[+|-]999
    \row \li \c E \li format as [-]9.9E[+|-]999
    \row \li \c f \li format as [-]9.9
    \row \li \c g \li use \c e or \c f format, whichever is the most concise
    \row \li \c G \li use \c E or \c f format, whichever is the most concise
    \endtable

    With 'e', 'E', and 'f', \a prec is the number of digits after the
    decimal point. With 'g' and 'G', \a prec is the maximum number of
    significant digits (trailing zeroes are omitted).

    \note The format of the number is not localized; the default C locale is
    used regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa toDouble()
*/

QByteArray &QByteArray::setNum(double n, char f, int prec)
{
    QLocaleData::DoubleForm form = QLocaleData::DFDecimal;
    uint flags = QLocaleData::ZeroPadExponent;

    char lower = asciiLower(uchar(f));
    if (f != lower)
        flags |= QLocaleData::CapitalEorX;
    f = lower;

    switch (f) {
        case 'f':
            form = QLocaleData::DFDecimal;
            break;
        case 'e':
            form = QLocaleData::DFExponent;
            break;
        case 'g':
            form = QLocaleData::DFSignificantDigits;
            break;
        default:
#if defined(QT_CHECK_RANGE)
            qWarning("QByteArray::setNum: Invalid format char '%c'", f);
#endif
            break;
    }

    *this = QLocaleData::c()->doubleToString(n, prec, form, -1, flags).toUtf8();
    return *this;
}

/*!
    \fn QByteArray &QByteArray::setNum(float n, char f, int prec)
    \overload

    Sets the byte array to the printed value of \a n, formatted in format
    \a f with precision \a prec, and returns a reference to the
    byte array.

    \note The format of the number is not localized; the default C locale is
    used regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa toFloat()
*/

/*!
    Returns a byte array containing the printed value of the number \a n to base
    \a base (ten by default). Bases 2 through 36 are supported, using letters
    for digits beyond 9: A is ten, B is eleven and so on.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 41

    \note The format of the number is not localized; the default C locale is
    used regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa setNum(), toInt()
*/
QByteArray QByteArray::number(int n, int base)
{
    QByteArray s;
    s.setNum(n, base);
    return s;
}

/*!
    \overload

    \sa toUInt()
*/
QByteArray QByteArray::number(uint n, int base)
{
    QByteArray s;
    s.setNum(n, base);
    return s;
}

/*!
    \overload

    \sa toLong()
*/
QByteArray QByteArray::number(long n, int base)
{
    QByteArray s;
    s.setNum(n, base);
    return s;
}

/*!
    \overload

    \sa toULong()
*/
QByteArray QByteArray::number(ulong n, int base)
{
    QByteArray s;
    s.setNum(n, base);
    return s;
}

/*!
    \overload

    \sa toLongLong()
*/
QByteArray QByteArray::number(qlonglong n, int base)
{
    QByteArray s;
    s.setNum(n, base);
    return s;
}

/*!
    \overload

    \sa toULongLong()
*/
QByteArray QByteArray::number(qulonglong n, int base)
{
    QByteArray s;
    s.setNum(n, base);
    return s;
}

/*!
    \overload

    Returns a byte array that contains the printed value of \a n,
    formatted in format \a f with precision \a prec.

    Argument \a n is formatted according to the \a f format specified,
    which is \c g by default, and can be any of the following:

    \table
    \header \li Format \li Meaning
    \row \li \c e \li format as [-]9.9e[+|-]999
    \row \li \c E \li format as [-]9.9E[+|-]999
    \row \li \c f \li format as [-]9.9
    \row \li \c g \li use \c e or \c f format, whichever is the most concise
    \row \li \c G \li use \c E or \c f format, whichever is the most concise
    \endtable

    With 'e', 'E', and 'f', \a prec is the number of digits after the
    decimal point. With 'g' and 'G', \a prec is the maximum number of
    significant digits (trailing zeroes are omitted).

    \snippet code/src_corelib_text_qbytearray.cpp 42

    \note The format of the number is not localized; the default C locale is
    used regardless of the user's locale. Use QLocale to perform locale-aware
    conversions between numbers and strings.

    \sa toDouble()
*/
QByteArray QByteArray::number(double n, char f, int prec)
{
    QByteArray s;
    s.setNum(n, f, prec);
    return s;
}

/*!
    \fn QByteArray QByteArray::fromRawData(const char *data, int size) constexpr

    Constructs a QByteArray that uses the first \a size bytes of the
    \a data array. The bytes are \e not copied. The QByteArray will
    contain the \a data pointer. The caller guarantees that \a data
    will not be deleted or modified as long as this QByteArray and any
    copies of it exist that have not been modified. In other words,
    because QByteArray is an \l{implicitly shared} class and the
    instance returned by this function contains the \a data pointer,
    the caller must not delete \a data or modify it directly as long
    as the returned QByteArray and any copies exist. However,
    QByteArray does not take ownership of \a data, so the QByteArray
    destructor will never delete the raw \a data, even when the
    last QByteArray referring to \a data is destroyed.

    A subsequent attempt to modify the contents of the returned
    QByteArray or any copy made from it will cause it to create a deep
    copy of the \a data array before doing the modification. This
    ensures that the raw \a data array itself will never be modified
    by QByteArray.

    Here is an example of how to read data using a QDataStream on raw
    data in memory without copying the raw data into a QByteArray:

    \snippet code/src_corelib_text_qbytearray.cpp 43

    \warning A byte array created with fromRawData() is \e not '\\0'-terminated,
    unless the raw data contains a '\\0' byte at position \a size. While that
    does not matter for QDataStream or functions like indexOf(), passing the
    byte array to a function accepting a \c{const char *} expected to be
    '\\0'-terminated will fail.

    \sa setRawData(), data(), constData()
*/

/*!
    \since 4.7

    Resets the QByteArray to use the first \a size bytes of the
    \a data array. The bytes are \e not copied. The QByteArray will
    contain the \a data pointer. The caller guarantees that \a data
    will not be deleted or modified as long as this QByteArray and any
    copies of it exist that have not been modified.

    This function can be used instead of fromRawData() to re-use
    existing QByteArray objects to save memory re-allocations.

    \sa fromRawData(), data(), constData()
*/
QByteArray &QByteArray::setRawData(const char *data, uint size)
{
    if (!data || !size) {
        clear();
    }
//    else if (d->isShared() || (d->flags() & Data::RawDataType) == 0) {
        *this = fromRawData(data, size);
//    } else {
//        d.size = size;
//        d.data() = const_cast<char *>(data);
//    }
    return *this;
}

namespace {
struct fromBase64_helper_result {
    qsizetype decodedLength;
    QByteArray::Base64DecodingStatus status;
};

fromBase64_helper_result fromBase64_helper(const char *input, qsizetype inputSize,
                                           char *output /* may alias input */,
                                           QByteArray::Base64Options options)
{
    fromBase64_helper_result result{ 0, QByteArray::Base64DecodingStatus::Ok };

    unsigned int buf = 0;
    int nbits = 0;

    qsizetype offset = 0;
    for (qsizetype i = 0; i < inputSize; ++i) {
        int ch = input[i];
        int d;

        if (ch >= 'A' && ch <= 'Z') {
            d = ch - 'A';
        } else if (ch >= 'a' && ch <= 'z') {
            d = ch - 'a' + 26;
        } else if (ch >= '0' && ch <= '9') {
            d = ch - '0' + 52;
        } else if (ch == '+' && (options & QByteArray::Base64UrlEncoding) == 0) {
            d = 62;
        } else if (ch == '-' && (options & QByteArray::Base64UrlEncoding) != 0) {
            d = 62;
        } else if (ch == '/' && (options & QByteArray::Base64UrlEncoding) == 0) {
            d = 63;
        } else if (ch == '_' && (options & QByteArray::Base64UrlEncoding) != 0) {
            d = 63;
        } else {
            if (options & QByteArray::AbortOnBase64DecodingErrors) {
                if (ch == '=') {
                    // can have 1 or 2 '=' signs, in both cases padding base64Size to
                    // a multiple of 4. Any other case is illegal.
                    if ((inputSize % 4) != 0) {
                        result.status = QByteArray::Base64DecodingStatus::IllegalInputLength;
                        return result;
                    } else if ((i == inputSize - 1) ||
                        (i == inputSize - 2 && input[++i] == '=')) {
                        d = -1; // ... and exit the loop, normally
                    } else {
                        result.status = QByteArray::Base64DecodingStatus::IllegalPadding;
                        return result;
                    }
                } else {
                    result.status = QByteArray::Base64DecodingStatus::IllegalCharacter;
                    return result;
                }
            } else {
                d = -1;
            }
        }

        if (d != -1) {
            buf = (buf << 6) | d;
            nbits += 6;
            if (nbits >= 8) {
                nbits -= 8;
                Q_ASSERT(offset < i);
                output[offset++] = buf >> nbits;
                buf &= (1 << nbits) - 1;
            }
        }
    }

    result.decodedLength = offset;
    return result;
}
} // anonymous namespace

/*!
    \fn QByteArray::FromBase64Result QByteArray::fromBase64Encoding(QByteArray &&base64, Base64Options options)
    \fn QByteArray::FromBase64Result QByteArray::fromBase64Encoding(const QByteArray &base64, Base64Options options)
    \since 5.15
    \overload

    Decodes the Base64 array \a base64, using the options
    defined by \a options. If \a options contains \c{IgnoreBase64DecodingErrors}
    (the default), the input is not checked for validity; invalid
    characters in the input are skipped, enabling the decoding process to
    continue with subsequent characters. If \a options contains
    \c{AbortOnBase64DecodingErrors}, then decoding will stop at the first
    invalid character.

    For example:

    \snippet code/src_corelib_text_qbytearray.cpp 44ter

    The algorithm used to decode Base64-encoded data is defined in \l{RFC 4648}.

    Returns a QByteArrayFromBase64Result object, containing the decoded
    data and a flag telling whether decoding was successful. If the
    \c{AbortOnBase64DecodingErrors} option was passed and the input
    data was invalid, it is unspecified what the decoded data contains.

    \sa toBase64()
*/
QByteArray::FromBase64Result QByteArray::fromBase64Encoding(QByteArray &&base64, Base64Options options)
{
    // try to avoid a detach when calling data(), as it would over-allocate
    // (we need less space when decoding than the one required by the full copy)
    if (base64.isDetached()) {
        const auto base64result = fromBase64_helper(base64.data(),
                                                    base64.size(),
                                                    base64.data(), // in-place
                                                    options);
        base64.truncate(int(base64result.decodedLength));
        return { std::move(base64), base64result.status };
    }

    return fromBase64Encoding(base64, options);
}


QByteArray::FromBase64Result QByteArray::fromBase64Encoding(const QByteArray &base64, Base64Options options)
{
    const auto base64Size = base64.size();
    QByteArray result((base64Size * 3) / 4, Qt::Uninitialized);
    const auto base64result = fromBase64_helper(base64.data(),
                                                base64Size,
                                                const_cast<char *>(result.constData()),
                                                options);
    result.truncate(int(base64result.decodedLength));
    return { std::move(result), base64result.status };
}

/*!
    \since 5.2

    Returns a decoded copy of the Base64 array \a base64, using the options
    defined by \a options. If \a options contains \c{IgnoreBase64DecodingErrors}
    (the default), the input is not checked for validity; invalid
    characters in the input are skipped, enabling the decoding process to
    continue with subsequent characters. If \a options contains
    \c{AbortOnBase64DecodingErrors}, then decoding will stop at the first
    invalid character.

    For example:

    \snippet code/src_corelib_text_qbytearray.cpp 44

    The algorithm used to decode Base64-encoded data is defined in \l{RFC 4648}.

    Returns the decoded data, or, if the \c{AbortOnBase64DecodingErrors}
    option was passed and the input data was invalid, an empty byte array.

    \note The fromBase64Encoding() function is recommended in new code.

    \sa toBase64(), fromBase64Encoding()
*/
QByteArray QByteArray::fromBase64(const QByteArray &base64, Base64Options options)
{
    if (auto result = fromBase64Encoding(base64, options))
        return std::move(result.decoded);
    return QByteArray();
}

/*!
    Returns a decoded copy of the hex encoded array \a hexEncoded. Input is not checked
    for validity; invalid characters in the input are skipped, enabling the
    decoding process to continue with subsequent characters.

    For example:

    \snippet code/src_corelib_text_qbytearray.cpp 45

    \sa toHex()
*/
QByteArray QByteArray::fromHex(const QByteArray &hexEncoded)
{
    QByteArray res((hexEncoded.size() + 1)/ 2, Qt::Uninitialized);
    uchar *result = (uchar *)res.data() + res.size();

    bool odd_digit = true;
    for (int i = hexEncoded.size() - 1; i >= 0; --i) {
        uchar ch = uchar(hexEncoded.at(i));
        int tmp = QtMiscUtils::fromHex(ch);
        if (tmp == -1)
            continue;
        if (odd_digit) {
            --result;
            *result = tmp;
            odd_digit = false;
        } else {
            *result |= tmp << 4;
            odd_digit = true;
        }
    }

    res.remove(0, result - (const uchar *)res.constData());
    return res;
}

/*! Returns a hex encoded copy of the byte array. The hex encoding uses the numbers 0-9 and
    the letters a-f.

    If \a separator is not '\0', the separator character is inserted between the hex bytes.

    Example:
    \snippet code/src_corelib_text_qbytearray.cpp 50

    \since 5.9
    \sa fromHex()
*/
QByteArray QByteArray::toHex(char separator) const
{
    if (isEmpty())
        return QByteArray();

    const int length = separator ? (size() * 3 - 1) : (size() * 2);
    QByteArray hex(length, Qt::Uninitialized);
    char *hexData = hex.data();
    const uchar *data = (const uchar *)this->data();
    for (int i = 0, o = 0; i < size(); ++i) {
        hexData[o++] = QtMiscUtils::toHexLower(data[i] >> 4);
        hexData[o++] = QtMiscUtils::toHexLower(data[i] & 0xf);

        if ((separator) && (o < length))
            hexData[o++] = separator;
    }
    return hex;
}

static void q_fromPercentEncoding(QByteArray *ba, char percent)
{
    if (ba->isEmpty())
        return;

    char *data = ba->data();
    const char *inputPtr = data;

    int i = 0;
    int len = ba->count();
    int outlen = 0;
    int a, b;
    char c;
    while (i < len) {
        c = inputPtr[i];
        if (c == percent && i + 2 < len) {
            a = inputPtr[++i];
            b = inputPtr[++i];

            if (a >= '0' && a <= '9') a -= '0';
            else if (a >= 'a' && a <= 'f') a = a - 'a' + 10;
            else if (a >= 'A' && a <= 'F') a = a - 'A' + 10;

            if (b >= '0' && b <= '9') b -= '0';
            else if (b >= 'a' && b <= 'f') b  = b - 'a' + 10;
            else if (b >= 'A' && b <= 'F') b  = b - 'A' + 10;

            *data++ = (char)((a << 4) | b);
        } else {
            *data++ = c;
        }

        ++i;
        ++outlen;
    }

    if (outlen != len)
        ba->truncate(outlen);
}

void q_fromPercentEncoding(QByteArray *ba)
{
    q_fromPercentEncoding(ba, '%');
}

/*!
    \since 4.4

    Returns a decoded copy of the URI/URL-style percent-encoded \a input.
    The \a percent parameter allows you to replace the '%' character for
    another (for instance, '_' or '=').

    For example:
    \snippet code/src_corelib_text_qbytearray.cpp 51

    \note Given invalid input (such as a string containing the sequence "%G5",
    which is not a valid hexadecimal number) the output will be invalid as
    well. As an example: the sequence "%G5" could be decoded to 'W'.

    \sa toPercentEncoding(), QUrl::fromPercentEncoding()
*/
QByteArray QByteArray::fromPercentEncoding(const QByteArray &input, char percent)
{
    if (input.isNull())
        return QByteArray();       // preserve null
    if (input.isEmpty())
        return QByteArray(input.data(), 0);

    QByteArray tmp = input;
    q_fromPercentEncoding(&tmp, percent);
    return tmp;
}

/*! \fn QByteArray QByteArray::fromStdString(const std::string &str)
    \since 5.4

    Returns a copy of the \a str string as a QByteArray.

    \sa toStdString(), QString::fromStdString()
*/

/*!
    \fn std::string QByteArray::toStdString() const
    \since 5.4

    Returns a std::string object with the data contained in this
    QByteArray.

    This operator is mostly useful to pass a QByteArray to a function
    that accepts a std::string object.

    \sa fromStdString(), QString::toStdString()
*/

static inline bool q_strchr(const char str[], char chr)
{
    if (!str) return false;

    const char *ptr = str;
    char c;
    while ((c = *ptr++))
        if (c == chr)
            return true;
    return false;
}

static void q_toPercentEncoding(QByteArray *ba, const char *dontEncode, const char *alsoEncode, char percent)
{
    if (ba->isEmpty())
        return;

    QByteArray input = *ba;
    int len = input.count();
    const char *inputData = input.constData();
    char *output = nullptr;
    int length = 0;

    for (int i = 0; i < len; ++i) {
        unsigned char c = *inputData++;
        if (((c >= 0x61 && c <= 0x7A) // ALPHA
             || (c >= 0x41 && c <= 0x5A) // ALPHA
             || (c >= 0x30 && c <= 0x39) // DIGIT
             || c == 0x2D // -
             || c == 0x2E // .
             || c == 0x5F // _
             || c == 0x7E // ~
             || q_strchr(dontEncode, c))
            && !q_strchr(alsoEncode, c)) {
            if (output)
                output[length] = c;
            ++length;
        } else {
            if (!output) {
                // detach now
                ba->resize(len*3); // worst case
                output = ba->data();
            }
            output[length++] = percent;
            output[length++] = QtMiscUtils::toHexUpper((c & 0xf0) >> 4);
            output[length++] = QtMiscUtils::toHexUpper(c & 0xf);
        }
    }
    if (output)
        ba->truncate(length);
}

void q_toPercentEncoding(QByteArray *ba, const char *exclude, const char *include)
{
    q_toPercentEncoding(ba, exclude, include, '%');
}

void q_normalizePercentEncoding(QByteArray *ba, const char *exclude)
{
    q_fromPercentEncoding(ba, '%');
    q_toPercentEncoding(ba, exclude, nullptr, '%');
}

/*!
    \since 4.4

    Returns a URI/URL-style percent-encoded copy of this byte array. The
    \a percent parameter allows you to override the default '%'
    character for another.

    By default, this function will encode all bytes that are not one of the
    following:

        ALPHA ("a" to "z" and "A" to "Z") / DIGIT (0 to 9) / "-" / "." / "_" / "~"

    To prevent bytes from being encoded pass them to \a exclude. To force bytes
    to be encoded pass them to \a include. The \a percent character is always
    encoded.

    Example:

    \snippet code/src_corelib_text_qbytearray.cpp 52

    The hex encoding uses the numbers 0-9 and the uppercase letters A-F.

    \sa fromPercentEncoding(), QUrl::toPercentEncoding()
*/
QByteArray QByteArray::toPercentEncoding(const QByteArray &exclude, const QByteArray &include,
                                         char percent) const
{
    if (isNull())
        return QByteArray();    // preserve null
    if (isEmpty())
        return QByteArray(data(), 0);

    QByteArray include2 = include;
    if (percent != '%')                        // the default
        if ((percent >= 0x61 && percent <= 0x7A) // ALPHA
            || (percent >= 0x41 && percent <= 0x5A) // ALPHA
            || (percent >= 0x30 && percent <= 0x39) // DIGIT
            || percent == 0x2D // -
            || percent == 0x2E // .
            || percent == 0x5F // _
            || percent == 0x7E) // ~
        include2 += percent;

    QByteArray result = *this;
    q_toPercentEncoding(&result, exclude.nulTerminated().constData(), include2.nulTerminated().constData(), percent);

    return result;
}

/*! \typedef QByteArray::ConstIterator
    \internal
*/

/*! \typedef QByteArray::Iterator
    \internal
*/

/*! \typedef QByteArray::const_iterator

    This typedef provides an STL-style const iterator for QByteArray.

    \sa QByteArray::const_reverse_iterator, QByteArray::iterator
*/

/*! \typedef QByteArray::iterator

    This typedef provides an STL-style non-const iterator for QByteArray.

    \sa QByteArray::reverse_iterator, QByteArray::const_iterator
*/

/*! \typedef QByteArray::const_reverse_iterator
    \since 5.6

    This typedef provides an STL-style const reverse iterator for QByteArray.

    \sa QByteArray::reverse_iterator, QByteArray::const_iterator
*/

/*! \typedef QByteArray::reverse_iterator
    \since 5.6

    This typedef provides an STL-style non-const reverse iterator for QByteArray.

    \sa QByteArray::const_reverse_iterator, QByteArray::iterator
*/

/*! \typedef QByteArray::size_type
    \internal
*/

/*! \typedef QByteArray::difference_type
    \internal
*/

/*! \typedef QByteArray::const_reference
    \internal
*/

/*! \typedef QByteArray::reference
    \internal
*/

/*! \typedef QByteArray::const_pointer
    \internal
*/

/*! \typedef QByteArray::pointer
    \internal
*/

/*! \typedef QByteArray::value_type
  \internal
 */

/*!
    \fn DataPtr &QByteArray::data_ptr()
    \internal
*/

/*!
    \typedef QByteArray::DataPtr
    \internal
*/

/*!
    \macro QByteArrayLiteral(ba)
    \relates QByteArray

    The macro generates the data for a QByteArray out of the string literal \a
    ba at compile time. Creating a QByteArray from it is free in this case, and
    the generated byte array data is stored in the read-only segment of the
    compiled object file.

    For instance:

    \snippet code/src_corelib_text_qbytearray.cpp 53

    Using QByteArrayLiteral instead of a double quoted plain C++ string literal
    can significantly speed up creation of QByteArray instances from data known
    at compile time.

    \sa QStringLiteral
*/

/*!
    \class QByteArray::FromBase64Result
    \inmodule QtCore
    \ingroup tools
    \since 5.15

    \brief The QByteArray::FromBase64Result class holds the result of
    a call to QByteArray::fromBase64Encoding.

    Objects of this class can be used to check whether the conversion
    was successful, and if so, retrieve the decoded QByteArray. The
    conversion operators defined for QByteArray::FromBase64Result make
    its usage straightforward:

    \snippet code/src_corelib_text_qbytearray.cpp 44ter

    Alternatively, it is possible to access the conversion status
    and the decoded data directly:

    \snippet code/src_corelib_text_qbytearray.cpp 44quater

    \sa QByteArray::fromBase64
*/

/*!
    \variable QByteArray::FromBase64Result::decoded

    Contains the decoded byte array.
*/

/*!
    \variable QByteArray::FromBase64Result::decodingStatus

    Contains whether the decoding was successful, expressed as a value
    of type QByteArray::Base64DecodingStatus.
*/

/*!
    \fn QByteArray::FromBase64Result::operator bool() const

    Returns whether the decoding was successful. This is equivalent
    to checking whether the \c{decodingStatus} member is equal to
    QByteArray::Base64DecodingStatus::Ok.
*/

/*!
    \fn QByteArray &QByteArray::FromBase64Result::operator*() const

    Returns the decoded byte array.
*/

/*!
    \fn bool operator==(const QByteArray::FromBase64Result &lhs, const QByteArray::FromBase64Result &rhs) noexcept
    \relates QByteArray::FromBase64Result

    Returns \c true if \a lhs and \a rhs are equal, otherwise returns \c false.

    \a lhs and \a rhs are equal if and only if they contain the same decoding
    status and, if the status is QByteArray::Base64DecodingStatus::Ok, if and
    only if they contain the same decoded data.
*/

/*!
    \fn bool operator!=(const QByteArray::FromBase64Result &lhs, const QByteArray::FromBase64Result &rhs) noexcept
    \relates QByteArray::FromBase64Result

    Returns \c true if \a lhs and \a rhs are different, otherwise returns \c false.
*/

/*!
    \relates QByteArray::FromBase64Result

    Returns the hash value for \a key, using
    \a seed to seed the calculation.
*/
size_t qHash(const QByteArray::FromBase64Result &key, size_t seed) noexcept
{
    return qHashMulti(seed, key.decoded, static_cast<int>(key.decodingStatus));
}

QT_END_NAMESPACE
