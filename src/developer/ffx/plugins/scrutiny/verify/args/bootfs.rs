// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "bootfs",
    description = "Verifies list of files in bootfs embedded in ZBI image against a golden file",
    example = r#"To verify bootfs on your current build:

    $ ffx scrutiny verify bootfs \
        --zbi $(fx get-build-dir)/obj/build/images/fuchsia/fuchsia/fuchsia.zbi \
        --golden /path/to/goldens/product.txt \
        --golden /path/to/goldens/board.txt"#,
    note = "Verifies all file paths in bootfs."
)]
pub struct Command {
    /// absolute or working directory-relative path to ZBI image file that contains bootfs.
    #[argh(option)]
    pub zbi: PathBuf,
    /// absolute or working directory-relative path(s) to golden file(s) for verifying bootfs paths.
    #[argh(option)]
    pub golden: Vec<PathBuf>,
    /// absolute or working directory-relative path(s) to golden file(s) for verifying bootfs packages.
    #[argh(option)]
    pub golden_packages: Vec<PathBuf>,
}
