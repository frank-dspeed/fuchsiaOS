# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Location of Linux sysroot in the platform build.
# Should we move this to a repository to be shared with the SDK?
_linux_sysroot = "prebuilt/third_party/sysroot/linux"

# List of all CPU architectures present in the current Linux sysroot.
# Each linux_sysroot_${arch} filegroup() should exclude those that do not
# correspond to its architecture, to avoid polluting the sandbox with
# un-necessary files.
_linux_sysroot_archs = [
    "aarch64",
    "arm",
    "i386",
    "x86_64",
]

def linux_sysroot(name, sysroot_arch, sysroot_path = _linux_sysroot):
    """Generates a filegroup() target that exposes a Linux sysroot for
       a given CPU architecture.

       Args:
           name: name of the generated filegroup().
           sysroot_arch: target CPU architecture for the filegroup, must be
               one of the values listed in _linux_sysroot_archs above.
           sysroot_path: path to the top-level sysroot directory.
    """
    if sysroot_arch not in _linux_sysroot_archs:
        fail("The sysroot_arch parameter must be one of: %s" % _linux_sysroot_archs)

    glob_patterns = [
        sysroot_path + "/etc/ld.so.conf.d/{arch}-linux-gnu.conf",
        sysroot_path + "/lib/{arch}-linux-gnu/**",
        sysroot_path + "/usr/lib/{arch}-linux-gnu/**",
        sysroot_path + "/usr/include/**",
    ]
    if sysroot_arch == "x86_64":
        glob_patterns += [sysroot_path + "/lib64/ld-linux-x86_64.so.2"]
    else:
        glob_patterns += [sysroot_path + "/lib/ld-linux-{arch}.so.*"]

    patterns = [
        pattern.format(arch = sysroot_arch)
        for pattern in glob_patterns
    ]

    exclude = [
        "**/{arch}-*/**"
        for arch in _linux_sysroot_archs
        if arch != sysroot_arch
    ]

    native.filegroup(
        name = name,
        srcs = native.glob(
            patterns,
            exclude = exclude,
            exclude_directories = 1,
        ),
        visibility = ["//visibility:public"],
    )
