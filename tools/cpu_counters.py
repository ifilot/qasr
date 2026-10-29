# SPDX-License-Identifier: GPL-3.0-only
"""Optional Linux x86-64 perf_event cycle measurements, in separate runs."""
import ctypes
import fcntl
import os
import platform
import signal
import struct
import subprocess


def measure_cycles(command):
    """Count user cycles for a stopped child and its inherited worker threads.

    Counter setup and reading occur in the parent. The child resumes immediately
    before exec, so the measurement includes the small fork/exec wrapper tail.
    Timed process-latency samples are collected separately without this wrapper.
    """
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        return {"available": False, "reason": "perf_event helper supports Linux x86-64"}
    attr = ctypes.create_string_buffer(128)
    # PERF_TYPE_HARDWARE / PERF_COUNT_HW_CPU_CYCLES, enabled/running read format.
    struct.pack_into("IIQQQQ", attr, 0, 0, 128, 0, 0, 0, 3)
    # disabled, inherit, exclude_kernel, exclude_hv
    struct.pack_into("Q", attr, 40, 1 | 2 | (1 << 5) | (1 << 6))
    libc = ctypes.CDLL(None, use_errno=True)
    pid = os.fork()
    if pid == 0:
        try:
            null = os.open(os.devnull, os.O_RDWR)
            for descriptor in (0, 1, 2):
                os.dup2(null, descriptor)
            if null > 2:
                os.close(null)
            os.kill(os.getpid(), signal.SIGSTOP)
            os.execv(command[0], command)
        except BaseException:
            os._exit(127)
    fd = -1
    reaped = False
    try:
        _, status = os.waitpid(pid, os.WUNTRACED)
        if not os.WIFSTOPPED(status):
            reaped = True
            raise RuntimeError("cycle measurement child failed before stop")
        fd = libc.syscall(ctypes.c_long(298), ctypes.byref(attr), ctypes.c_long(pid),
                          ctypes.c_long(-1), ctypes.c_long(-1), ctypes.c_long(0))
        reason = os.strerror(ctypes.get_errno()) if fd < 0 else None
        if fd >= 0:
            fcntl.ioctl(fd, 0x2403, 0)  # PERF_EVENT_IOC_RESET
            fcntl.ioctl(fd, 0x2400, 0)  # PERF_EVENT_IOC_ENABLE
        os.kill(pid, signal.SIGCONT)
        _, status = os.waitpid(pid, 0)
        reaped = True
        code = os.waitstatus_to_exitcode(status)
        if code:
            raise subprocess.CalledProcessError(code, command)
        if fd < 0:
            return {"available": False, "reason": reason}
        fcntl.ioctl(fd, 0x2401, 0)  # PERF_EVENT_IOC_DISABLE
        raw = os.read(fd, 24)
        if len(raw) != 24:
            return {"available": False, "reason": "counter returned incomplete data"}
        cycles, enabled, running = struct.unpack("QQQ", raw)
        if not cycles or not running:
            return {"available": False, "reason": "hardware cycle counter inactive"}
        return {"available": True, "cycles": cycles, "time_enabled_ns": enabled,
                "time_running_ns": running, "scaled_cycles": round(cycles*enabled/running)}
    finally:
        if fd >= 0:
            os.close(fd)
        if not reaped:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
