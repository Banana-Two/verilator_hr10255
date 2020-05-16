// -*- mode: C++; c-file-style: "cc-mode" -*-
//=============================================================================
//
// THIS MODULE IS PUBLICLY LICENSED
//
// Copyright 2001-2020 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//=============================================================================
///
/// \file
/// \brief C++ Tracing in VCD Format
///
//=============================================================================
// SPDIFF_OFF

// clang-format off

#include "verilatedos.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>

#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
# include <io.h>
#else
# include <unistd.h>
#endif

#include "verilated_intrinsics.h"

// SPDIFF_ON

#ifndef O_LARGEFILE  // For example on WIN32
# define O_LARGEFILE 0
#endif
#ifndef O_NONBLOCK
# define O_NONBLOCK 0
#endif
#ifndef O_CLOEXEC
# define O_CLOEXEC 0
#endif

// clang-format on

// This size comes form VCD allowing use of printable ASCII characters between
// '!' and '~' inclusive, which are a total of 94 different values. Encoding a
// 32 bit code hence needs a maximum of ceil(log94(2**32-1)) == 5 bytes.
#define VL_TRACE_MAX_VCD_CODE_SIZE 5  ///< Maximum length of a VCD string code
// We use 8 bytes per code in a suffix buffer array.
// 1 byte optional separator + VL_TRACE_MAX_VCD_CODE_SIZE bytes for code
// + 1 byte '\n' + 1 byte suffix size. This luckily comes out to a power of 2,
// meaning the array can be aligned such that entries never straddle multiple
// cache-lines.
#define VL_TRACE_SUFFIX_ENTRY_SIZE 8  ///< Size of a suffix entry

//=============================================================================
// Specialization of the generics for this trace format

#define VL_DERIVED_T VerilatedVcd
#include "verilated_trace_imp.cpp"
#undef VL_DERIVED_T

//=============================================================================
// VerilatedVcdImp
/// Base class to hold some static state
/// This is an internally used class

class VerilatedVcdSingleton {
private:
    typedef std::vector<VerilatedVcd*> VcdVec;
    struct Singleton {
        VerilatedMutex s_vcdMutex;  ///< Protect the singleton
        VcdVec s_vcdVecp VL_GUARDED_BY(s_vcdMutex);  ///< List of all created traces
    };
    static Singleton& singleton() {
        static Singleton s;
        return s;
    }

public:
    static void pushVcd(VerilatedVcd* vcdp) VL_EXCLUDES(singleton().s_vcdMutex) {
        VerilatedLockGuard lock(singleton().s_vcdMutex);
        singleton().s_vcdVecp.push_back(vcdp);
    }
    static void removeVcd(const VerilatedVcd* vcdp) VL_EXCLUDES(singleton().s_vcdMutex) {
        VerilatedLockGuard lock(singleton().s_vcdMutex);
        VcdVec::iterator pos
            = find(singleton().s_vcdVecp.begin(), singleton().s_vcdVecp.end(), vcdp);
        if (pos != singleton().s_vcdVecp.end()) { singleton().s_vcdVecp.erase(pos); }
    }
    static void flush_all() VL_EXCLUDES(singleton().s_vcdMutex) VL_MT_UNSAFE_ONE {
        // Thread safety: Although this function is protected by a mutex so
        // perhaps in the future we can allow tracing in separate threads,
        // vcdp->flush() assumes call from single thread
        VerilatedLockGuard lock(singleton().s_vcdMutex);
        for (VcdVec::const_iterator it = singleton().s_vcdVecp.begin();
             it != singleton().s_vcdVecp.end(); ++it) {
            VerilatedVcd* vcdp = *it;
            vcdp->flush();
        }
    }
};

//=============================================================================
//=============================================================================
//=============================================================================
// VerilatedVcdFile

bool VerilatedVcdFile::open(const std::string& name) VL_MT_UNSAFE {
    m_fd = ::open(name.c_str(),
                  O_CREAT | O_WRONLY | O_TRUNC | O_LARGEFILE | O_NONBLOCK | O_CLOEXEC, 0666);
    return m_fd >= 0;
}

void VerilatedVcdFile::close() VL_MT_UNSAFE { ::close(m_fd); }

ssize_t VerilatedVcdFile::write(const char* bufp, ssize_t len) VL_MT_UNSAFE {
    return ::write(m_fd, bufp, len);
}

//=============================================================================
//=============================================================================
//=============================================================================
// Opening/Closing

VerilatedVcd::VerilatedVcd(VerilatedVcdFile* filep)
    : m_isOpen(false)
    , m_rolloverMB(0)
    , m_modDepth(0) {
    // Not in header to avoid link issue if header is included without this .cpp file
    m_fileNewed = (filep == NULL);
    m_filep = m_fileNewed ? new VerilatedVcdFile : filep;
    m_namemapp = NULL;
    m_evcd = false;
    m_wrChunkSize = 8 * 1024;
    m_wrBufp = new char[m_wrChunkSize * 8];
    m_wrFlushp = m_wrBufp + m_wrChunkSize * 6;
    m_writep = m_wrBufp;
    m_wroteBytes = 0;
    m_suffixesp = NULL;
}

void VerilatedVcd::open(const char* filename) {
    m_assertOne.check();
    if (isOpen()) return;

    // Set member variables
    m_filename = filename;  // "" is ok, as someone may overload open
    VerilatedVcdSingleton::pushVcd(this);

    // SPDIFF_OFF
    // Set callback so an early exit will flush us
    Verilated::flushCb(&flush_all);

    // SPDIFF_ON
    openNext(m_rolloverMB != 0);
    if (!isOpen()) return;

    dumpHeader();

    // Get the direct access pointer to the code strings
    m_suffixesp = &m_suffixes[0];  // Note: C++11 m_suffixes.data();

    // When using rollover, the first chunk contains the header only.
    if (m_rolloverMB) openNext(true);
}

void VerilatedVcd::openNext(bool incFilename) {
    // Open next filename in concat sequence, mangle filename if
    // incFilename is true.
    m_assertOne.check();
    closePrev();  // Close existing
    if (incFilename) {
        // Find _0000.{ext} in filename
        std::string name = m_filename;
        size_t pos = name.rfind('.');
        if (pos > 8 && 0 == strncmp("_cat", name.c_str() + pos - 8, 4)
            && isdigit(name.c_str()[pos - 4]) && isdigit(name.c_str()[pos - 3])
            && isdigit(name.c_str()[pos - 2]) && isdigit(name.c_str()[pos - 1])) {
            // Increment code.
            if ((++(name[pos - 1])) > '9') {
                name[pos - 1] = '0';
                if ((++(name[pos - 2])) > '9') {
                    name[pos - 2] = '0';
                    if ((++(name[pos - 3])) > '9') {
                        name[pos - 3] = '0';
                        if ((++(name[pos - 4])) > '9') { name[pos - 4] = '0'; }
                    }
                }
            }
        } else {
            // Append _cat0000
            name.insert(pos, "_cat0000");
        }
        m_filename = name;
    }
    if (m_filename[0] == '|') {
        assert(0);  // Not supported yet.
    } else {
        // cppcheck-suppress duplicateExpression
        if (!m_filep->open(m_filename)) {
            // User code can check isOpen()
            m_isOpen = false;
            return;
        }
    }
    m_isOpen = true;
    fullDump(true);  // First dump must be full
    m_wroteBytes = 0;
}

bool VerilatedVcd::preChangeDump() {
    if (VL_UNLIKELY(m_rolloverMB && m_wroteBytes > m_rolloverMB)) { openNext(true); }
    return isOpen();
}

void VerilatedVcd::emitTimeChange(vluint64_t timeui) {
    printStr("#");
    printQuad(timeui);
    printStr("\n");
}

void VerilatedVcd::makeNameMap() {
    // Take signal information from each module and build m_namemapp
    deleteNameMap();
    m_namemapp = new NameMap;

    VerilatedTrace<VerilatedVcd>::traceInit();

    // Though not speced, it's illegal to generate a vcd with signals
    // not under any module - it crashes at least two viewers.
    // If no scope was specified, prefix everything with a "top"
    // This comes from user instantiations with no name - IE Vtop("").
    bool nullScope = false;
    for (NameMap::const_iterator it = m_namemapp->begin(); it != m_namemapp->end(); ++it) {
        const std::string& hiername = it->first;
        if (!hiername.empty() && hiername[0] == '\t') nullScope = true;
    }
    if (nullScope) {
        NameMap* newmapp = new NameMap;
        for (NameMap::const_iterator it = m_namemapp->begin(); it != m_namemapp->end(); ++it) {
            const std::string& hiername = it->first;
            const std::string& decl = it->second;
            std::string newname = std::string("top");
            if (hiername[0] != '\t') newname += ' ';
            newname += hiername;
            newmapp->insert(std::make_pair(newname, decl));
        }
        deleteNameMap();
        m_namemapp = newmapp;
    }
}

void VerilatedVcd::deleteNameMap() {
    if (m_namemapp) VL_DO_CLEAR(delete m_namemapp, m_namemapp = NULL);
}

VerilatedVcd::~VerilatedVcd() {
    close();
    if (m_wrBufp) VL_DO_CLEAR(delete[] m_wrBufp, m_wrBufp = NULL);
    deleteNameMap();
    if (m_filep && m_fileNewed) VL_DO_CLEAR(delete m_filep, m_filep = NULL);
    VerilatedVcdSingleton::removeVcd(this);
}

void VerilatedVcd::closePrev() {
    // This function is on the flush() call path
    if (!isOpen()) return;

    VerilatedTrace<VerilatedVcd>::flush();
    bufferFlush();
    m_isOpen = false;
    m_filep->close();
}

void VerilatedVcd::closeErr() {
    // This function is on the flush() call path
    // Close due to an error.  We might abort before even getting here,
    // depending on the definition of vl_fatal.
    if (!isOpen()) return;

    // No buffer flush, just fclose
    m_isOpen = false;
    m_filep->close();  // May get error, just ignore it
}

void VerilatedVcd::close() {
    // This function is on the flush() call path
    m_assertOne.check();
    if (!isOpen()) return;
    if (m_evcd) {
        printStr("$vcdclose ");
        printQuad(timeLastDump());
        printStr(" $end\n");
    }
    closePrev();
    // closePrev() called VerilatedTrace<VerilatedVcd>::flush(), so we just
    // need to shut down the tracing thread here.
    VerilatedTrace<VerilatedVcd>::close();
}

void VerilatedVcd::flush() {
    VerilatedTrace<VerilatedVcd>::flush();
    bufferFlush();
}

void VerilatedVcd::printStr(const char* str) {
    // Not fast...
    while (*str) {
        *m_writep++ = *str++;
        bufferCheck();
    }
}

void VerilatedVcd::printQuad(vluint64_t n) {
    char buf[100];
    sprintf(buf, "%" VL_PRI64 "u", n);
    printStr(buf);
}

void VerilatedVcd::bufferResize(vluint64_t minsize) {
    // minsize is size of largest write.  We buffer at least 8 times as much data,
    // writing when we are 3/4 full (with thus 2*minsize remaining free)
    if (VL_UNLIKELY(minsize > m_wrChunkSize)) {
        char* oldbufp = m_wrBufp;
        m_wrChunkSize = minsize * 2;
        m_wrBufp = new char[m_wrChunkSize * 8];
        memcpy(m_wrBufp, oldbufp, m_writep - oldbufp);
        m_writep = m_wrBufp + (m_writep - oldbufp);
        m_wrFlushp = m_wrBufp + m_wrChunkSize * 6;
        VL_DO_CLEAR(delete[] oldbufp, oldbufp = NULL);
    }
}

void VerilatedVcd::bufferFlush() VL_MT_UNSAFE_ONE {
    // This function can be called from the trace thread
    // This function is on the flush() call path
    // We add output data to m_writep.
    // When it gets nearly full we dump it using this routine which calls write()
    // This is much faster than using buffered I/O
    m_assertOne.check();
    if (VL_UNLIKELY(!isOpen())) return;
    char* wp = m_wrBufp;
    while (true) {
        ssize_t remaining = (m_writep - wp);
        if (remaining == 0) break;
        errno = 0;
        ssize_t got = m_filep->write(wp, remaining);
        if (got > 0) {
            wp += got;
            m_wroteBytes += got;
        } else if (VL_UNCOVERABLE(got < 0)) {
            if (VL_UNCOVERABLE(errno != EAGAIN && errno != EINTR)) {
                // write failed, presume error (perhaps out of disk space)
                std::string msg = std::string("VerilatedVcd::bufferFlush: ") + strerror(errno);
                VL_FATAL_MT("", 0, "", msg.c_str());
                closeErr();
                break;
            }
        }
    }

    // Reset buffer
    m_writep = m_wrBufp;
}

//=============================================================================
// VCD string code

char* VerilatedVcd::writeCode(char* writep, vluint32_t code) {
    *writep++ = static_cast<char>('!' + code % 94);
    code /= 94;
    while (code) {
        code--;
        *writep++ = static_cast<char>('!' + code % 94);
        code /= 94;
    }
    return writep;
}

//=============================================================================
// Definitions

void VerilatedVcd::printIndent(int level_change) {
    if (level_change < 0) m_modDepth += level_change;
    assert(m_modDepth >= 0);
    for (int i = 0; i < m_modDepth; i++) printStr(" ");
    if (level_change > 0) m_modDepth += level_change;
}

void VerilatedVcd::dumpHeader() {
    printStr("$version Generated by VerilatedVcd $end\n");
    time_t time_str = time(NULL);
    printStr("$date ");
    printStr(ctime(&time_str));
    printStr(" $end\n");

    printStr("$timescale ");
    printStr(timeResStr().c_str());  // lintok-begin-on-ref
    printStr(" $end\n");

    makeNameMap();

    // Signal header
    assert(m_modDepth == 0);
    printIndent(1);
    printStr("\n");

    // We detect the spaces in module names to determine hierarchy.  This
    // allows signals to be declared without fixed ordering, which is
    // required as Verilog signals might be separately declared from
    // SC module signals.

    // Print the signal names
    const char* lastName = "";
    for (NameMap::const_iterator it = m_namemapp->begin(); it != m_namemapp->end(); ++it) {
        const std::string& hiernamestr = it->first;
        const std::string& decl = it->second;

        // Determine difference between the old and new names
        const char* hiername = hiernamestr.c_str();
        const char* lp = lastName;
        const char* np = hiername;
        lastName = hiername;

        // Skip common prefix, it must break at a space or tab
        for (; *np && (*np == *lp); np++, lp++) {}
        while (np != hiername && *np && *np != ' ' && *np != '\t') {
            np--;
            lp--;
        }
        // printf("hier %s\n  lp=%s\n  np=%s\n",hiername,lp,np);

        // Any extra spaces in last name are scope ups we need to do
        bool first = true;
        for (; *lp; lp++) {
            if (*lp == ' ' || (first && *lp != '\t')) {
                printIndent(-1);
                printStr("$upscope $end\n");
            }
            first = false;
        }

        // Any new spaces are scope downs we need to do
        while (*np) {
            if (*np == ' ') np++;
            if (*np == '\t') break;  // tab means signal name starts
            printIndent(1);
            printStr("$scope module ");
            for (; *np && *np != ' ' && *np != '\t'; np++) {
                if (*np == '[') {
                    printStr("(");
                } else if (*np == ']') {
                    printStr(")");
                } else {
                    *m_writep++ = *np;
                }
            }
            printStr(" $end\n");
        }

        printIndent(0);
        printStr(decl.c_str());
    }

    while (m_modDepth > 1) {
        printIndent(-1);
        printStr("$upscope $end\n");
    }

    printIndent(-1);
    printStr("$enddefinitions $end\n\n\n");
    assert(m_modDepth == 0);

    // Reclaim storage
    deleteNameMap();
}

void VerilatedVcd::declare(vluint32_t code, const char* name, const char* wirep, bool array,
                           int arraynum, bool tri, bool bussed, int msb, int lsb) {
    const int bits = ((msb > lsb) ? (msb - lsb) : (lsb - msb)) + 1;

    VerilatedTrace<VerilatedVcd>::declCode(code, bits, tri);

    if (m_suffixes.size() <= nextCode() * VL_TRACE_SUFFIX_ENTRY_SIZE) {
        m_suffixes.resize(nextCode() * VL_TRACE_SUFFIX_ENTRY_SIZE * 2, 0);
    }

    // Make sure write buffer is large enough (one character per bit), plus header
    bufferResize(bits + 1024);

    // Split name into basename
    // Spaces and tabs aren't legal in VCD signal names, so:
    // Space separates each level of scope
    // Tab separates final scope from signal name
    // Tab sorts before spaces, so signals nicely will print before scopes
    // Note the hiername may be nothing, if so we'll add "\t{name}"
    std::string nameasstr = name;
    if (!moduleName().empty()) {
        nameasstr = moduleName() + scopeEscape() + nameasstr;  // Optional ->module prefix
    }
    std::string hiername;
    std::string basename;
    for (const char* cp = nameasstr.c_str(); *cp; cp++) {
        if (isScopeEscape(*cp)) {
            // Ahh, we've just read a scope, not a basename
            if (!hiername.empty()) hiername += " ";
            hiername += basename;
            basename = "";
        } else {
            basename += *cp;
        }
    }
    hiername += "\t" + basename;

    // Print reference
    std::string decl = "$var ";
    if (m_evcd) {
        decl += "port";
    } else {
        decl += wirep;  // usually "wire"
    }

    char buf[1000];
    sprintf(buf, " %2d ", bits);
    decl += buf;
    if (m_evcd) {
        sprintf(buf, "<%u", code);
        decl += buf;
    } else {
        // Add string code to decl
        char* const endp = writeCode(buf, code);
        *endp = '\0';
        decl += buf;
        // Build suffix array entry
        char* const entryp = &m_suffixes[code * VL_TRACE_SUFFIX_ENTRY_SIZE];
        const size_t length = endp - buf;
        assert(length <= VL_TRACE_MAX_VCD_CODE_SIZE);
        // 1 bit values don't have a ' ' separator between value and string code
        const bool isBit = bits == 1;
        entryp[0] = ' ';  // Separator
        std::strcpy(entryp + !isBit, buf);  // Code (overwrite separator if isBit)
        entryp[length + !isBit] = '\n';  // Replace '\0' with line termination '\n'
        // Set length of suffix (used to increment write pointer)
        entryp[VL_TRACE_SUFFIX_ENTRY_SIZE - 1] = !isBit + length + 1;
    }
    decl += " ";
    decl += basename;
    if (array) {
        sprintf(buf, "(%d)", arraynum);
        decl += buf;
        hiername += buf;
    }
    if (bussed) {
        sprintf(buf, " [%d:%d]", msb, lsb);
        decl += buf;
    }
    decl += " $end\n";
    m_namemapp->insert(std::make_pair(hiername, decl));
}

void VerilatedVcd::declBit(vluint32_t code, const char* name, bool array, int arraynum) {
    declare(code, name, "wire", array, arraynum, false, false, 0, 0);
}
void VerilatedVcd::declBus(vluint32_t code, const char* name, bool array, int arraynum, int msb,
                           int lsb) {
    declare(code, name, "wire", array, arraynum, false, true, msb, lsb);
}
void VerilatedVcd::declQuad(vluint32_t code, const char* name, bool array, int arraynum, int msb,
                            int lsb) {
    declare(code, name, "wire", array, arraynum, false, true, msb, lsb);
}
void VerilatedVcd::declArray(vluint32_t code, const char* name, bool array, int arraynum, int msb,
                             int lsb) {
    declare(code, name, "wire", array, arraynum, false, true, msb, lsb);
}
void VerilatedVcd::declFloat(vluint32_t code, const char* name, bool array, int arraynum) {
    declare(code, name, "real", array, arraynum, false, false, 31, 0);
}
void VerilatedVcd::declDouble(vluint32_t code, const char* name, bool array, int arraynum) {
    declare(code, name, "real", array, arraynum, false, false, 63, 0);
}
#ifdef VL_TRACE_VCD_OLD_API
void VerilatedVcd::declTriBit(vluint32_t code, const char* name, bool array, int arraynum) {
    declare(code, name, "wire", array, arraynum, true, false, 0, 0);
}
void VerilatedVcd::declTriBus(vluint32_t code, const char* name, bool array, int arraynum, int msb,
                              int lsb) {
    declare(code, name, "wire", array, arraynum, true, true, msb, lsb);
}
void VerilatedVcd::declTriQuad(vluint32_t code, const char* name, bool array, int arraynum,
                               int msb, int lsb) {
    declare(code, name, "wire", array, arraynum, true, true, msb, lsb);
}
void VerilatedVcd::declTriArray(vluint32_t code, const char* name, bool array, int arraynum,
                                int msb, int lsb) {
    declare(code, name, "wire", array, arraynum, true, true, msb, lsb);
}
#endif  //  VL_TRACE_VCD_OLD_API

//=============================================================================
// Trace rendering prinitives

void VerilatedVcd::finishLine(vluint32_t code, char* writep) {
    const char* const suffixp = m_suffixesp + code * VL_TRACE_SUFFIX_ENTRY_SIZE;
    // Copy the whole suffix (this avoid having hard to predict branches which
    // helps a lot). Note: The maximum length of the suffix is
    // VL_TRACE_MAX_VCD_CODE_SIZE + 2 == 7, but we unroll this here for speed.
#ifdef VL_X86_64
    // Copy the whole 8 bytes in one go, this works on little-endian machines
    // supporting unaligned stores.
    *reinterpret_cast<vluint64_t*>(writep) = *reinterpret_cast<const vluint64_t*>(suffixp);
#else
    // Portable variant
    writep[0] = suffixp[0];
    writep[1] = suffixp[1];
    writep[2] = suffixp[2];
    writep[3] = suffixp[3];
    writep[4] = suffixp[4];
    writep[5] = suffixp[5];
    writep[6] = '\n';  // The 6th index is always '\n' if it's relevant, no need to fetch it.
#endif
    // Now write back the write pointer incremented by the actual size of the
    // suffix, which was stored in the last byte of the suffix buffer entry.
    m_writep = writep + suffixp[VL_TRACE_SUFFIX_ENTRY_SIZE - 1];
    bufferCheck();
}

//=============================================================================
// emit* trace routines

// Note: emit* are only ever called from one place (full* in
// verilated_trace_imp.cpp, which is included in this file at the top),
// so always inline them.

VL_ATTR_ALWINLINE
void VerilatedVcd::emitBit(vluint32_t code, CData newval) {
    // Don't prefetch suffix as it's a bit too late;
    char* wp = m_writep;
    *wp++ = '0' | static_cast<char>(newval);
    finishLine(code, wp);
}

VL_ATTR_ALWINLINE
void VerilatedVcd::emitCData(vluint32_t code, CData newval, int bits) {
    char* wp = m_writep;
    *wp++ = 'b';
    cvtCDataToStr(wp, newval << (VL_BYTESIZE - bits));
    finishLine(code, wp + bits);
}

VL_ATTR_ALWINLINE
void VerilatedVcd::emitSData(vluint32_t code, SData newval, int bits) {
    char* wp = m_writep;
    *wp++ = 'b';
    cvtSDataToStr(wp, newval << (VL_SHORTSIZE - bits));
    finishLine(code, wp + bits);
}

VL_ATTR_ALWINLINE
void VerilatedVcd::emitIData(vluint32_t code, IData newval, int bits) {
    char* wp = m_writep;
    *wp++ = 'b';
    cvtIDataToStr(wp, newval << (VL_IDATASIZE - bits));
    finishLine(code, wp + bits);
}

VL_ATTR_ALWINLINE
void VerilatedVcd::emitQData(vluint32_t code, QData newval, int bits) {
    char* wp = m_writep;
    *wp++ = 'b';
    cvtQDataToStr(wp, newval << (VL_QUADSIZE - bits));
    finishLine(code, wp + bits);
}

VL_ATTR_ALWINLINE
void VerilatedVcd::emitWData(vluint32_t code, const WData* newvalp, int bits) {
    int words = VL_WORDS_I(bits);
    char* wp = m_writep;
    *wp++ = 'b';
    // Handle the most significant word
    const int bitsInMSW = VL_BITBIT_E(bits) ? VL_BITBIT_E(bits) : VL_EDATASIZE;
    cvtEDataToStr(wp, newvalp[--words] << (VL_EDATASIZE - bitsInMSW));
    wp += bitsInMSW;
    // Handle the remaining words
    while (words > 0) {
        cvtEDataToStr(wp, newvalp[--words]);
        wp += VL_EDATASIZE;
    }
    finishLine(code, wp);
}

VL_ATTR_ALWINLINE
void VerilatedVcd::emitFloat(vluint32_t code, float newval) {
    char* wp = m_writep;
    // Buffer can't overflow before sprintf; we sized during declaration
    sprintf(wp, "r%.16g", static_cast<double>(newval));
    wp += strlen(wp);
    finishLine(code, wp);
}

VL_ATTR_ALWINLINE
void VerilatedVcd::emitDouble(vluint32_t code, double newval) {
    char* wp = m_writep;
    // Buffer can't overflow before sprintf; we sized during declaration
    sprintf(wp, "r%.16g", newval);
    wp += strlen(wp);
    finishLine(code, wp);
}

#ifdef VL_TRACE_VCD_OLD_API

void VerilatedVcd::fullBit(vluint32_t code, const vluint32_t newval) {
    // Note the &1, so we don't require clean input -- makes more common no change case faster
    *oldp(code) = newval;
    *m_writep++ = ('0' + static_cast<char>(newval & 1));
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullBus(vluint32_t code, const vluint32_t newval, int bits) {
    *oldp(code) = newval;
    *m_writep++ = 'b';
    for (int bit = bits - 1; bit >= 0; --bit) {
        *m_writep++ = ((newval & (1L << bit)) ? '1' : '0');
    }
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullQuad(vluint32_t code, const vluint64_t newval, int bits) {
    (*(reinterpret_cast<vluint64_t*>(oldp(code)))) = newval;
    *m_writep++ = 'b';
    for (int bit = bits - 1; bit >= 0; --bit) {
        *m_writep++ = ((newval & (VL_ULL(1) << bit)) ? '1' : '0');
    }
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullArray(vluint32_t code, const vluint32_t* newval, int bits) {
    for (int word = 0; word < (((bits - 1) / 32) + 1); ++word) { oldp(code)[word] = newval[word]; }
    *m_writep++ = 'b';
    for (int bit = bits - 1; bit >= 0; --bit) {
        *m_writep++ = ((newval[(bit / 32)] & (1L << (bit & 0x1f))) ? '1' : '0');
    }
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullArray(vluint32_t code, const vluint64_t* newval, int bits) {
    for (int word = 0; word < (((bits - 1) / 64) + 1); ++word) { oldp(code)[word] = newval[word]; }
    *m_writep++ = 'b';
    for (int bit = bits - 1; bit >= 0; --bit) {
        *m_writep++ = ((newval[(bit / 64)] & (VL_ULL(1) << (bit & 0x3f))) ? '1' : '0');
    }
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullTriBit(vluint32_t code, const vluint32_t newval, const vluint32_t newtri) {
    oldp(code)[0] = newval;
    oldp(code)[1] = newtri;
    *m_writep++ = "01zz"[newval | (newtri << 1)];
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullTriBus(vluint32_t code, const vluint32_t newval, const vluint32_t newtri,
                              int bits) {
    oldp(code)[0] = newval;
    oldp(code)[1] = newtri;
    *m_writep++ = 'b';
    for (int bit = bits - 1; bit >= 0; --bit) {
        *m_writep++ = "01zz"[((newval >> bit) & 1) | (((newtri >> bit) & 1) << 1)];
    }
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullTriQuad(vluint32_t code, const vluint64_t newval, const vluint32_t newtri,
                               int bits) {
    (*(reinterpret_cast<vluint64_t*>(oldp(code)))) = newval;
    (*(reinterpret_cast<vluint64_t*>(oldp(code + 1)))) = newtri;
    *m_writep++ = 'b';
    for (int bit = bits - 1; bit >= 0; --bit) {
        *m_writep++
            = "01zz"[((newval >> bit) & VL_ULL(1)) | (((newtri >> bit) & VL_ULL(1)) << VL_ULL(1))];
    }
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullTriArray(vluint32_t code, const vluint32_t* newvalp,
                                const vluint32_t* newtrip, int bits) {
    for (int word = 0; word < (((bits - 1) / 32) + 1); ++word) {
        oldp(code)[word * 2] = newvalp[word];
        oldp(code)[word * 2 + 1] = newtrip[word];
    }
    *m_writep++ = 'b';
    for (int bit = bits - 1; bit >= 0; --bit) {
        vluint32_t valbit = (newvalp[(bit / 32)] >> (bit & 0x1f)) & 1;
        vluint32_t tribit = (newtrip[(bit / 32)] >> (bit & 0x1f)) & 1;
        *m_writep++ = "01zz"[valbit | (tribit << 1)];
    }
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullDouble(vluint32_t code, const double newval) {
    // cppcheck-suppress invalidPointerCast
    (*(reinterpret_cast<double*>(oldp(code)))) = newval;
    // Buffer can't overflow before sprintf; we sized during declaration
    sprintf(m_writep, "r%.16g", newval);
    m_writep += strlen(m_writep);
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullFloat(vluint32_t code, const float newval) {
    // cppcheck-suppress invalidPointerCast
    (*(reinterpret_cast<float*>(oldp(code)))) = newval;
    // Buffer can't overflow before sprintf; we sized during declaration
    sprintf(m_writep, "r%.16g", static_cast<double>(newval));
    m_writep += strlen(m_writep);
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullBitX(vluint32_t code) {
    *m_writep++ = 'x';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullBusX(vluint32_t code, int bits) {
    *m_writep++ = 'b';
    for (int bit = bits - 1; bit >= 0; --bit) *m_writep++ = 'x';
    *m_writep++ = ' ';
    m_writep = writeCode(m_writep, code);
    *m_writep++ = '\n';
    bufferCheck();
}
void VerilatedVcd::fullQuadX(vluint32_t code, int bits) { fullBusX(code, bits); }
void VerilatedVcd::fullArrayX(vluint32_t code, int bits) { fullBusX(code, bits); }

#endif  // VL_TRACE_VCD_OLD_API

//======================================================================
// Static members

void VerilatedVcd::flush_all() VL_MT_UNSAFE_ONE { VerilatedVcdSingleton::flush_all(); }

//======================================================================
//======================================================================
//======================================================================

#ifdef VERILATED_VCD_TEST
#include <iostream>

vluint32_t v1, v2, s1, s2[3];
vluint32_t tri96[3];
vluint32_t tri96__tri[3];
vluint64_t quad96[2];
vluint8_t ch;
vluint64_t timestamp = 1;
double doub = 0.0;
float flo = 0.0f;

void vcdInit(void*, VerilatedVcd* vcdp, vluint32_t) {
    vcdp->scopeEscape('.');
    vcdp->module("top");
    /**/ vcdp->declBus(0x2, "v1", -1, 0, 5, 1);
    /**/ vcdp->declBus(0x3, "v2", -1, 0, 6, 1);
    /**/ vcdp->module("top.sub1");
    /***/ vcdp->declBit(0x4, "s1", -1, 0);
    /***/ vcdp->declBit(0x5, "ch", -1, 0);
    /**/ vcdp->module("top.sub2");
    /***/ vcdp->declArray(0x6, "s2", -1, 0, 40, 3);
    // Note need to add 3 for next code.
    vcdp->module("top2");
    /**/ vcdp->declBus(0x2, "t2v1", -1, 0, 4, 1);
    /**/ vcdp->declTriBit(0x10, "io1", -1, 0);
    /**/ vcdp->declTriBus(0x12, "io5", -1, 0, 4, 0);
    /**/ vcdp->declTriArray(0x16, "io96", -1, 0, 95, 0);
    /**/  // Note need to add 6 for next code.
    /**/ vcdp->declDouble(0x1c, "doub", -1, 0);
    /**/  // Note need to add 2 for next code.
    /**/ vcdp->declDouble(0x1e, "flo", -1, 0);
    /**/ vcdp->declArray(0x20, "q2", -1, 0, 95, 0);
    // Note need to add 4 for next code.
}

void vcdFull(void*, VerilatedVcd* vcdp) {
    vcdp->fullBus(0x2, v1, 5);
    vcdp->fullBus(0x3, v2, 7);
    vcdp->fullBit(0x4, s1);
    vcdp->fullBus(0x5, ch, 2);
    vcdp->fullArray(0x6, &s2[0], 38);
    vcdp->fullTriBit(0x10, tri96[0] & 1, tri96__tri[0] & 1);
    vcdp->fullTriBus(0x12, tri96[0] & 0x1f, tri96__tri[0] & 0x1f, 5);
    vcdp->fullTriArray(0x16, tri96, tri96__tri, 96);
    vcdp->fullDouble(0x1c, doub);
    vcdp->fullFloat(0x1e, flo);
    vcdp->fullArray(0x20, &quad96[0], 96);
}

void vcdChange(void*, VerilatedVcd* vcdp) {
    vcdp->chgBus(0x2, v1, 5);
    vcdp->chgBus(0x3, v2, 7);
    vcdp->chgBit(0x4, s1);
    vcdp->chgBus(0x5, ch, 2);
    vcdp->chgArray(0x6, &s2[0], 38);
    vcdp->chgTriBit(0x10, tri96[0] & 1, tri96__tri[0] & 1);
    vcdp->chgTriBus(0x12, tri96[0] & 0x1f, tri96__tri[0] & 0x1f, 5);
    vcdp->chgTriArray(0x16, tri96, tri96__tri, 96);
    vcdp->chgDouble(0x1c, doub);
    vcdp->chgFloat(0x1e, flo);
    vcdp->chgArray(0x20, &quad96[0], 96);
}

// clang-format off
void vcdTestMain(const char* filenamep) {
    v1 = v2 = s1 = 0;
    s2[0] = s2[1] = s2[2] = 0;
    tri96[2] = tri96[1] = tri96[0] = 0;
    tri96__tri[2] = tri96__tri[1] = tri96__tri[0] = ~0;
    quad96[1] = quad96[0] = 0;
    ch = 0;
    doub = 0;
    {
        VerilatedVcdC* vcdp = new VerilatedVcdC;
        vcdp->spTrace()->addInitCb(&vcdInit, 0);
        vcdp->spTrace()->addFullCb(&vcdFull, 0);
        vcdp->spTrace()->addChgCb(&vcdChange, 0);
        vcdp->open(filenamep);
        // Dumping
        vcdp->dump(++timestamp);
        v1 = 0xfff;
        tri96[2] = 4; tri96[1] = 2; tri96[0] = 1;
        tri96__tri[2] = tri96__tri[1] = tri96__tri[0] = ~0;  // Still tri
        quad96[1] = 0xffffffff; quad96[0] = 0;
        doub = 1.5;
        flo = 1.4f;
        vcdp->dump(++timestamp);
        v2 = 0x1;
        s2[1] = 2;
        tri96__tri[2] = tri96__tri[1] = tri96__tri[0] = 0;  // enable w/o data change
        quad96[1] = 0; quad96[0] = ~0;
        doub = -1.66e13;
        flo = 0.123f;
        vcdp->dump(++timestamp);
        ch = 2;
        tri96[2] = ~4; tri96[1] = ~2; tri96[0] = ~1;
        doub = -3.33e-13;
        vcdp->dump(++timestamp);
        vcdp->dump(++timestamp);
# ifdef VERILATED_VCD_TEST_64BIT
        vluint64_t bytesPerDump = VL_ULL(15);
        for (vluint64_t i = 0; i < ((VL_ULL(1) << 32) / bytesPerDump); i++) {
            v1 = i;
            vcdp->dump(++timestamp);
        }
# endif
        vcdp->close();
    }
}
#endif
// clang-format on

//********************************************************************
// ;compile-command: "v4make test_regress/t/t_trace_c_api.pl"
//
// Local Variables:
// End:
