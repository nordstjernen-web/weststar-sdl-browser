#!/usr/bin/env python3
"""check-xp-imports.py: fail when a Windows binary imports what Windows XP
SP3 (32-bit) does not export.

    python3 scripts/check-xp-imports.py [--cache DIR] weststar.exe [x.dll ...]

Reads the import tables (and delay-load imports) of each PE file given, and
checks every imported function against the exports of XP SP3's system DLLs.
A DLL that is itself among the files given is checked against that file's
own export table instead. It also checks the PE header: an i386 image whose
operating-system and subsystem versions ask for no more than 5.1 (XP).

Where the export lists come from, and the limits of that:

  * msvcrt.dll: mingw-w64's msvcrt.def.in, which lists the exports of
    msvcrt.dll by the Windows version that added them. Everything up to and
    including the "added in Windows XP" section counts (i386 entries).
    That list is maintained against the real DLLs and is close to exact.

  * Every other DLL: ReactOS's .spec files. ReactOS implements Windows
    Server 2003 SP2 (NT 5.2) and marks later exports with -version=0x600+
    and the like; entries whose range leaves out 0x501 are dropped. That
    is a near superset of XP SP3: NT 5.2 added a few exports XP never had,
    and the ones known to be misused are listed in NOT_ON_XP below and
    refused. Wine-derived specs in ReactOS sometimes carry a later export
    without a version mark; such a function would slip through. So a pass
    is strong evidence, not proof: the real test is running on XP.

  * A DLL that is not part of XP (bcrypt, dwmapi, dxgi, d3d11, api-ms-win-*,
    ...) fails outright, unless it was given on the command line.

The lists are fetched once from pinned commits (network needed the first
time) and kept in --cache (default: ~/.cache/weststar-xp-exports).
Exit status: 0 when all is well, 1 when something is not on XP, 2 on error.
"""

import argparse
import os
import re
import struct
import sys
import urllib.request

MINGW_COMMIT = "1ea0ab26879ee46a15e8b0c44081bb6c00334a8c"
MINGW_MSVCRT = ("https://raw.githubusercontent.com/mingw-w64/mingw-w64/"
                + MINGW_COMMIT + "/mingw-w64-crt/lib-common/msvcrt.def.in")
REACTOS_COMMIT = "a5ccec99ed843493ae8d5b8a4af45b65b4b9e2bc"
REACTOS_RAW = ("https://raw.githubusercontent.com/reactos/reactos/"
               + REACTOS_COMMIT + "/")

# The system DLLs of 32-bit Windows XP SP3 with an export list, and where
# ReactOS keeps its spec. d3d9 is DirectX 9.0c, part of XP SP2 and later.
REACTOS_SPECS = {
    "advapi32": "dll/win32/advapi32/advapi32.spec",
    "cfgmgr32": "dll/win32/cfgmgr32/cfgmgr32.spec",
    "comctl32": "dll/win32/comctl32/comctl32.spec",
    "comdlg32": "dll/win32/comdlg32/comdlg32.spec",
    "crypt32": "dll/win32/crypt32/crypt32.spec",
    "d3d9": "dll/directx/wine/d3d9/d3d9.spec",
    "dbghelp": "dll/win32/dbghelp/dbghelp.spec",
    "dinput8": "dll/directx/wine/dinput8/dinput8.spec",
    "dnsapi": "dll/win32/dnsapi/dnsapi.spec",
    "dsound": "dll/directx/wine/dsound/dsound.spec",
    "gdi32": "win32ss/gdi/gdi32/gdi32.spec",
    "hid": "dll/win32/hid/hid.spec",
    "imm32": "win32ss/user/imm32/imm32.spec",
    "iphlpapi": "dll/win32/iphlpapi/iphlpapi.spec",
    "kernel32": "dll/win32/kernel32/kernel32.spec",
    "msimg32": "dll/win32/msimg32/msimg32.spec",
    "ntdll": "dll/ntdll/def/ntdll.spec",
    "ole32": "dll/win32/ole32/ole32.spec",
    "oleaut32": "dll/win32/oleaut32/oleaut32.spec",
    "psapi": "dll/win32/psapi/psapi.spec",
    "rpcrt4": "dll/win32/rpcrt4/rpcrt4.spec",
    "secur32": "dll/win32/secur32/secur32.spec",
    "setupapi": "dll/win32/setupapi/setupapi.spec",
    "shell32": "dll/win32/shell32/shell32.spec",
    "shlwapi": "dll/win32/shlwapi/shlwapi.spec",
    "user32": "win32ss/user/user32/user32.spec",
    "userenv": "dll/win32/userenv/userenv.spec",
    "usp10": "dll/win32/usp10/usp10.spec",
    "uxtheme": "dll/win32/uxtheme/uxtheme.spec",
    "version": "dll/win32/version/version.spec",
    "winmm": "dll/win32/winmm/winmm.spec",
    "wldap32": "dll/win32/wldap32/wldap32.spec",
    "ws2_32": "dll/win32/ws2_32/ws2_32.spec",
    "wsock32": "dll/win32/wsock32/wsock32.spec",
}

# Exported by Windows Server 2003 (so present in ReactOS's lists without a
# version mark, or wrongly unmarked) but not by 32-bit Windows XP SP3, per
# the "minimum supported client" of Microsoft's documentation.
NOT_ON_XP = {
    "kernel32": {
        "AddVectoredContinueHandler", "RemoveVectoredContinueHandler",
        "FlsAlloc", "FlsFree", "FlsGetValue", "FlsSetValue",
        "GetCurrentProcessorNumber", "GetLargePageMinimum",
        "GetProcessIdOfThread", "GetThreadId", "ReOpenFile",
        "GetSystemFileCacheSize", "SetSystemFileCacheSize",
        "SetThreadStackGuarantee", "Wow64DisableWow64FsRedirection",
        "Wow64EnableWow64FsRedirection", "Wow64RevertWow64FsRedirection",
        "GetTickCount64", "InitOnceExecuteOnce", "InitOnceBeginInitialize",
        "InitOnceComplete", "InitializeSRWLock", "AcquireSRWLockExclusive",
        "AcquireSRWLockShared", "ReleaseSRWLockExclusive",
        "ReleaseSRWLockShared", "InitializeConditionVariable",
        "SleepConditionVariableCS", "SleepConditionVariableSRW",
        "WakeConditionVariable", "WakeAllConditionVariable",
        "CreateFile2", "GetFileInformationByHandleEx",
        "SetFileInformationByHandle", "GetFinalPathNameByHandleW",
        "CompareStringEx", "LocaleNameToLCID", "LCIDToLocaleName",
        "GetUserDefaultLocaleName", "GetSystemDefaultLocaleName",
        "GetThreadPreferredUILanguages", "GetUserPreferredUILanguages",
        "GetDynamicTimeZoneInformation", "CreateSymbolicLinkW",
        "QueryFullProcessImageNameW", "CancelIoEx", "GetErrorMode",
        "InitializeCriticalSectionEx", "CreateEventExW", "CreateMutexExW",
        "CreateSemaphoreExW", "WaitOnAddress", "WakeByAddressSingle",
        "GetSystemTimePreciseAsFileTime", "GetQueuedCompletionStatusEx",
        "SetFileCompletionNotificationModes", "IdnToAscii", "IdnToUnicode",
    },
    "advapi32": {
        "RegGetValueA", "RegGetValueW", "RegDeleteKeyExA", "RegDeleteKeyExW",
        "RegDeleteTreeA", "RegDeleteTreeW", "RegCopyTreeA", "RegCopyTreeW",
        "RegSetKeyValueA", "RegSetKeyValueW", "RegLoadMUIStringW",
    },
    "shell32": {"SHGetKnownFolderPath", "SHGetKnownFolderIDList",
                "SHCreateItemFromParsingName"},
    "ws2_32": {"inet_pton", "inet_ntop", "InetPtonW", "InetNtopW",
               "WSAPoll", "GetAddrInfoExW", "GetAddrInfoW"},
    "user32": {"SetProcessDPIAware", "IsProcessDPIAware",
               "GetDpiForWindow", "GetDpiForSystem",
               "SetProcessDpiAwarenessContext", "ChangeWindowMessageFilter",
               "ChangeWindowMessageFilterEx", "AddClipboardFormatListener",
               "RemoveClipboardFormatListener", "RegisterTouchWindow",
               "GetTouchInputInfo", "CloseTouchInputHandle",
               "GetPointerInfo", "GetPointerType", "EnableNonClientDpiScaling",
               "AdjustWindowRectExForDpi", "GetSystemMetricsForDpi",
               "SystemParametersInfoForDpi", "EnableMouseInPointer",
               "RegisterPowerSettingNotification",
               "UnregisterPowerSettingNotification",
               "ShutdownBlockReasonCreate", "ShutdownBlockReasonDestroy"},
    "iphlpapi": {"if_nametoindex", "if_indextoname", "ConvertInterfaceLuidToNameW",
                 "GetIfEntry2", "GetIfTable2", "FreeMibTable",
                 "NotifyIpInterfaceChange", "CancelMibChangeNotify2"},
}

# DLLs that are on XP SP3 but whose exports are not checked here.
XP_UNCHECKED = set()


def fetch(url, path):
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with urllib.request.urlopen(url, timeout=60) as r:
        data = r.read()
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def version_includes_xp(spec):
    """True when a ReactOS -version= range covers NT 5.1 (0x501)."""
    xp = 0x501
    for part in spec.split(","):
        part = part.strip()
        if part.endswith("+"):
            if xp >= int(part[:-1], 16):
                return True
        elif "-" in part:
            lo, hi = part.split("-", 1)
            if int(lo, 16) <= xp <= int(hi, 16):
                return True
        elif int(part, 16) == xp:
            return True
    return False


SPEC_LINE = re.compile(r"^\s*(@|\d+)\s+(\w+)\s+(.*)$")


def parse_reactos_spec(text):
    names = set()
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        m = SPEC_LINE.match(line)
        if not m:
            continue
        kind, rest = m.group(2), m.group(3).split()
        if kind not in ("stdcall", "cdecl", "fastcall", "thiscall",
                        "varargs", "extern", "stub", "stdcall64"):
            continue
        noname = False
        ok = True
        while rest and rest[0].startswith("-"):
            flag = rest.pop(0)
            if flag.startswith("-version="):
                ok = ok and version_includes_xp(flag[len("-version="):])
            elif flag.startswith("-arch="):
                archs = flag[len("-arch="):].split(",")
                if any(a.startswith("!") for a in archs):
                    ok = ok and not any(a in ("!i386", "!win32", "!x86")
                                        for a in archs)
                else:
                    ok = ok and any(a in ("i386", "win32", "x86")
                                    for a in archs)
            elif flag == "-noname":
                noname = True
        if not ok or noname or not rest:
            continue
        name = rest[0].split("(", 1)[0]
        if name:
            names.add(name)
    return names


DEF_MACRO = re.compile(r"^(F[A-Z0-9_]*)\((.*)\)\s*$")
INLINE_MACRO = re.compile(r"(F[A-Z0-9_]*)\(([^)]*)\)")
I386_MACROS = {"F_I386", "F_X86_ANY", "F_NON_X64", "F_NON_ARM64", "F_NON_I386"}


def parse_msvcrt_def(text):
    """msvcrt.dll exports of 32-bit XP SP3 from mingw-w64's msvcrt.def.in:
    every i386 entry before the section of those Server 2003 added."""
    names = set()
    ifstack = []
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("; These symbols were added in Windows Server 2003 OS"):
            break
        if line.startswith("#ifdef"):
            ifstack.append(line.split()[1] == "__i386__")
            continue
        if line.startswith("#if"):
            ifstack.append(False)
            continue
        if line.startswith("#endif"):
            if ifstack:
                ifstack.pop()
            continue
        if line.startswith("#") or not all(ifstack):
            continue
        line = line.split(";", 1)[0].strip()
        if not line or line.startswith(("EXPORTS", "LIBRARY")):
            continue
        m = DEF_MACRO.match(line)
        if m:
            # F_NON_I386 entries are in the i386 DLL too; mingw-w64 merely
            # links its own replacement for them.
            if m.group(1) not in I386_MACROS:
                continue
            line = m.group(2).strip()
        # A macro after the name ("__p__iob F_NON_I386(== __iob_func)") is
        # the i386 import library's business: expanded as for i386.
        line = INLINE_MACRO.sub(
            lambda m: m.group(2) if m.group(1) in I386_MACROS
            and m.group(1) != "F_NON_I386" else "", line).strip()
        # "import == export": the DLL's own name is the right-hand one.
        if "==" in line:
            line = line.split("==", 1)[1].strip()
        names.add(line.split()[0])
    return names


def load_exports(cache):
    exports = {}
    path = os.path.join(cache, "mingw-w64-" + MINGW_COMMIT[:12], "msvcrt.def.in")
    fetch(MINGW_MSVCRT, path)
    with open(path, encoding="utf-8", errors="replace") as f:
        exports["msvcrt"] = parse_msvcrt_def(f.read())
    for dll, rel in REACTOS_SPECS.items():
        path = os.path.join(cache, "reactos-" + REACTOS_COMMIT[:12], rel)
        fetch(REACTOS_RAW + rel, path)
        with open(path, encoding="utf-8", errors="replace") as f:
            names = parse_reactos_spec(f.read())
        exports[dll] = names - NOT_ON_XP.get(dll, set())
    return exports


# ---- PE parsing ----

class PE:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError("not a PE file")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise ValueError("no PE signature")
        (self.machine, nsect, _, _, _, optsize, self.characteristics) = \
            struct.unpack_from("<HHIIIHH", d, pe + 4)
        opt = pe + 24
        self.magic = struct.unpack_from("<H", d, opt)[0]
        pe32plus = self.magic == 0x20B
        (self.os_major, self.os_minor, _, _, self.subsys_major,
         self.subsys_minor) = struct.unpack_from("<HHHHHH", d, opt + 40)
        self.subsystem = struct.unpack_from("<H", d, opt + 68)[0]
        self.image_base = struct.unpack_from(
            "<Q" if self.magic == 0x20B else "<I", d,
            opt + (24 if self.magic == 0x20B else 28))[0]
        ddir = opt + (112 if pe32plus else 96)
        ndirs = struct.unpack_from("<I", d, ddir - 4)[0]
        self.dirs = [struct.unpack_from("<II", d, ddir + 8 * i)
                     for i in range(ndirs)]
        self.pe32plus = pe32plus
        self.sections = []
        sect = opt + optsize
        for i in range(nsect):
            (vsize, va, rawsize, rawptr) = struct.unpack_from(
                "<IIII", d, sect + 40 * i + 8)
            self.sections.append((va, max(vsize, rawsize), rawptr))

    def off(self, rva):
        for va, size, raw in self.sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError("RVA 0x%x outside the sections" % rva)

    def cstr(self, rva):
        o = self.off(rva)
        return self.data[o:self.data.index(b"\0", o)].decode("latin-1")

    def _thunks(self, rva):
        size, flag = (8, 1 << 63) if self.pe32plus else (4, 1 << 31)
        fmt = "<Q" if self.pe32plus else "<I"
        o = self.off(rva)
        while True:
            v = struct.unpack_from(fmt, self.data, o)[0]
            if v == 0:
                return
            if v & flag:
                yield "#%d" % (v & 0xFFFF)
            else:
                yield self.cstr((v & 0x7FFFFFFF) + 2)
            o += size

    def imports(self):
        """{dll: set(functions)} from the import and delay-import tables."""
        out = {}
        if len(self.dirs) > 1 and self.dirs[1][0]:
            o = self.off(self.dirs[1][0])
            while True:
                oft, _, _, name, ft = struct.unpack_from("<IIIII", self.data, o)
                if name == 0:
                    break
                funcs = out.setdefault(self.cstr(name), set())
                funcs.update(self._thunks(oft or ft))
                o += 20
        if len(self.dirs) > 13 and self.dirs[13][0]:
            o = self.off(self.dirs[13][0])
            while True:
                (attrs, name, _, _, int_rva, _, _, _) = struct.unpack_from(
                    "<8I", self.data, o)
                if name == 0:
                    break
                # Bit 0 clear: an old descriptor of addresses, not RVAs.
                base = 0 if attrs & 1 else -self.image_base
                funcs = out.setdefault(self.cstr(name + base), set())
                funcs.update(self._thunks(int_rva + base))
                o += 32
        return out

    def exports(self):
        if not self.dirs or not self.dirs[0][0]:
            return set()
        o = self.off(self.dirs[0][0])
        (_, _, _, _, _, _, nfuncs, nnames, _, names_rva, _) = \
            struct.unpack_from("<IIHHIIIIIII", self.data, o)
        names = set()
        for i in range(nnames):
            rva = struct.unpack_from("<I", self.data,
                                     self.off(names_rva) + 4 * i)[0]
            names.add(self.cstr(rva))
        return names


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="+", help="weststar.exe and any DLLs shipped with it")
    ap.add_argument("--cache", default=os.path.join(
        os.path.expanduser("~"), ".cache", "weststar-xp-exports"),
        help="where the downloaded export lists are kept")
    ap.add_argument("--list", action="store_true",
                    help="print every import, not only the failures")
    args = ap.parse_args()

    try:
        exports = load_exports(args.cache)
    except OSError as e:
        print("check-xp-imports: cannot fetch the export lists: %s" % e,
              file=sys.stderr)
        return 2

    pes = {}
    for path in args.files:
        try:
            pes[os.path.basename(path).lower()] = (path, PE(path))
        except (OSError, ValueError) as e:
            print("check-xp-imports: %s: %s" % (path, e), file=sys.stderr)
            return 2
    shipped = {name[:-4] if name.endswith(".dll") else name: pe.exports()
               for name, (_, pe) in pes.items()}

    bad = 0
    for name, (path, pe) in pes.items():
        header = []
        if pe.machine != 0x14C:
            header.append("machine 0x%x, not i386" % pe.machine)
        if (pe.os_major, pe.os_minor) > (5, 1):
            header.append("OS version %d.%d" % (pe.os_major, pe.os_minor))
        if (pe.subsys_major, pe.subsys_minor) > (5, 1):
            header.append("subsystem version %d.%d"
                          % (pe.subsys_major, pe.subsys_minor))
        print("%s: i386=%s, OS version %d.%d, subsystem %d version %d.%d"
              % (path, pe.machine == 0x14C, pe.os_major, pe.os_minor,
                 pe.subsystem, pe.subsys_major, pe.subsys_minor))
        for h in header:
            print("  NOT XP: PE header: " + h)
            bad += 1
        for dll, funcs in sorted(pe.imports().items(), key=lambda x: x[0].lower()):
            key = dll.lower()
            key = key[:-4] if key.endswith(".dll") else key
            known = shipped.get(key)
            source = "shipped" if known is not None else "XP SP3"
            if known is None:
                known = exports.get(key)
            if known is None:
                if key in XP_UNCHECKED:
                    print("  %s: on XP, exports not checked (%d functions)"
                          % (dll, len(funcs)))
                    continue
                print("  NOT XP: %s: no such DLL on Windows XP (%s)"
                      % (dll, ", ".join(sorted(funcs))))
                bad += 1
                continue
            missing = sorted(f for f in funcs
                             if not f.startswith("#") and f not in known)
            print("  %s: %d functions, checked against %s%s"
                  % (dll, len(funcs), source,
                     "" if not args.list else ": " + ", ".join(sorted(funcs))))
            for f in missing:
                print("  NOT XP: %s!%s" % (dll, f))
                bad += 1
    if bad:
        print("check-xp-imports: %d import(s) or header field(s) not on "
              "Windows XP SP3" % bad)
        return 1
    print("check-xp-imports: everything imported is on Windows XP SP3")
    return 0


if __name__ == "__main__":
    sys.exit(main())
