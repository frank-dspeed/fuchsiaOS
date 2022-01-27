// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, sys::zx_thread_state_general_regs_t, AsHandleRef, HandleBased};
use process_builder::{elf_load, elf_parse};
use std::ffi::{CStr, CString};
use std::sync::Arc;
use zerocopy::AsBytes;

use crate::from_status_like_fdio;
use crate::fs::FileHandle;
use crate::logging::*;
use crate::mm::*;
use crate::task::*;
use crate::types::*;
use crate::{errno, error};

fn populate_initial_stack(
    stack_vmo: &zx::Vmo,
    argv: &Vec<CString>,
    environ: &Vec<CString>,
    mut auxv: Vec<(u32, u64)>,
    stack_base: UserAddress,
    original_stack_start_addr: UserAddress,
) -> Result<UserAddress, Errno> {
    let mut stack_pointer = original_stack_start_addr;
    let write_stack = |data: &[u8], addr: UserAddress| {
        stack_vmo
            .write(data, (addr - stack_base) as u64)
            .map_err(|status| from_status_like_fdio!(status))
    };

    let mut string_data = vec![];
    for arg in argv {
        string_data.extend_from_slice(arg.as_bytes_with_nul());
    }
    for env in environ {
        string_data.extend_from_slice(env.as_bytes_with_nul());
    }
    stack_pointer -= string_data.len();
    let strings_addr = stack_pointer;
    write_stack(string_data.as_slice(), strings_addr)?;

    let mut random_seed = [0; 16];
    zx::cprng_draw(&mut random_seed);
    stack_pointer -= random_seed.len();
    let random_seed_addr = stack_pointer;
    write_stack(&random_seed, random_seed_addr)?;

    auxv.push((AT_RANDOM, random_seed_addr.ptr() as u64));
    auxv.push((AT_NULL, 0));

    // After the remainder (argc/argv/environ/auxv) is pushed, the stack pointer must be 16 byte
    // aligned. This is required by the ABI and assumed by the compiler to correctly align SSE
    // operations. But this can't be done after it's pushed, since it has to be right at the top of
    // the stack. So we collect it all, align the stack appropriately now that we know the size,
    // and push it all at once.
    let mut main_data = vec![];
    // argc
    let argc: u64 = argv.len() as u64;
    main_data.extend_from_slice(&argc.to_ne_bytes());
    // argv
    const ZERO: [u8; 8] = [0; 8];
    let mut next_string_addr = strings_addr;
    for arg in argv {
        main_data.extend_from_slice(&next_string_addr.ptr().to_ne_bytes());
        next_string_addr += arg.as_bytes_with_nul().len();
    }
    main_data.extend_from_slice(&ZERO);
    // environ
    for env in environ {
        main_data.extend_from_slice(&next_string_addr.ptr().to_ne_bytes());
        next_string_addr += env.as_bytes_with_nul().len();
    }
    main_data.extend_from_slice(&ZERO);
    // auxv
    for (tag, val) in auxv {
        main_data.extend_from_slice(&(tag as u64).to_ne_bytes());
        main_data.extend_from_slice(&(val as u64).to_ne_bytes());
    }

    // Time to push.
    stack_pointer -= main_data.len();
    stack_pointer -= stack_pointer.ptr() % 16;
    write_stack(main_data.as_slice(), stack_pointer)?;

    return Ok(stack_pointer);
}

struct LoadedElf {
    headers: elf_parse::Elf64Headers,
    file_base: usize,
    vaddr_bias: usize,
    vmo: zx::Vmo,
}

// TODO: Improve the error reporting produced by this function by mapping ElfParseError to Errno more precisely.
fn elf_parse_error_to_errno(err: elf_parse::ElfParseError) -> Errno {
    log::warn!("elf parse error: {:?}", err);
    errno!(EINVAL)
}

// TODO: Improve the error reporting produced by this function by mapping ElfLoadError to Errno more precisely.
fn elf_load_error_to_errno(err: elf_load::ElfLoadError) -> Errno {
    log::warn!("elf load error: {:?}", err);
    errno!(EINVAL)
}

struct Mapper<'a> {
    file: &'a FileHandle,
    mm: &'a MemoryManager,
}
impl elf_load::Mapper for Mapper<'_> {
    fn map(
        &self,
        vmar_offset: usize,
        vmo: &zx::Vmo,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<usize, zx::Status> {
        let vmo = Arc::new(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?);
        self.mm
            .map(
                self.mm.base_addr + vmar_offset,
                vmo,
                vmo_offset,
                length,
                flags,
                MappingOptions::empty(),
                Some(self.file.name.clone()),
            )
            .map_err(|e| {
                // TODO: Find a way to propagate this errno to the caller.
                log::error!("elf map error: {:?}", e);
                zx::Status::INVALID_ARGS
            })
            .map(|addr| addr.ptr())
    }
}

fn load_elf(elf: FileHandle, elf_vmo: zx::Vmo, mm: &MemoryManager) -> Result<LoadedElf, Errno> {
    let headers = elf_parse::Elf64Headers::from_vmo(&elf_vmo).map_err(elf_parse_error_to_errno)?;
    let elf_info = elf_load::loaded_elf_info(&headers);
    let file_base = match headers.file_header().elf_type() {
        Ok(elf_parse::ElfType::SharedObject) => {
            mm.get_random_base(elf_info.high - elf_info.low).ptr()
        }
        Ok(elf_parse::ElfType::Executable) => elf_info.low,
        _ => return error!(EINVAL),
    };
    let vaddr_bias = file_base.wrapping_sub(elf_info.low);
    let mapper = Mapper { file: &elf, mm };
    elf_load::map_elf_segments(&elf_vmo, &headers, &mapper, mm.base_addr.ptr(), vaddr_bias)
        .map_err(elf_load_error_to_errno)?;
    Ok(LoadedElf { headers, file_base, vaddr_bias, vmo: elf_vmo })
}

pub struct ThreadStartInfo {
    pub entry: UserAddress,
    pub stack: UserAddress,

    /// The address of the DT_DEBUG entry.
    pub dt_debug_address: Option<UserAddress>,
}

impl ThreadStartInfo {
    pub fn to_registers(&self) -> zx_thread_state_general_regs_t {
        let mut registers = zx_thread_state_general_regs_t::default();
        registers.rip = self.entry.ptr() as u64;
        registers.rsp = self.stack.ptr() as u64;
        registers
    }
}

/// Holds a resolved ELF VMO and associated parameters necessary for an execve call.
pub struct ResolvedElf {
    /// A file handle to the resolved ELF executable.
    pub file: FileHandle,
    /// A VMO to the resolved ELF executable.
    pub vmo: zx::Vmo,
    /// An ELF interpreter, if specified in the ELF executable header.
    pub interp: Option<ResolvedInterpElf>,
    /// Arguments to be passed to the new process.
    pub argv: Vec<CString>,
    /// The environment to initialize for the new process.
    pub environ: Vec<CString>,
}

/// Holds a resolved ELF interpreter VMO.
pub struct ResolvedInterpElf {
    /// A file handle to the resolved ELF interpreter.
    file: FileHandle,
    /// A VMO to the resolved ELF interpreter.
    vmo: zx::Vmo,
}

/// Resolves a file handle into a validated executable ELF.
pub fn resolve_executable(
    current_task: &CurrentTask,
    executable: FileHandle,
    argv: Vec<CString>,
    environ: Vec<CString>,
) -> Result<ResolvedElf, Errno> {
    let vmo =
        executable.get_vmo(current_task, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_EXECUTE)?;
    let elf_headers = elf_parse::Elf64Headers::from_vmo(&vmo).map_err(elf_parse_error_to_errno)?;
    let interp = if let Some(interp_hdr) = elf_headers
        .program_header_with_type(elf_parse::SegmentType::Interp)
        .map_err(|_| errno!(EINVAL))?
    {
        // The ELF header specified an ELF interpreter.
        // Read the path and load this ELF as well.
        let mut interp = vec![0; interp_hdr.filesz as usize];
        vmo.read(&mut interp, interp_hdr.offset as u64)
            .map_err(|status| from_status_like_fdio!(status))?;
        let interp = CStr::from_bytes_with_nul(&interp).map_err(|_| errno!(EINVAL))?;
        let interp_file = current_task.open_file(interp.to_bytes(), OpenFlags::RDONLY)?;
        let interp_vmo = interp_file
            .get_vmo(current_task, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_EXECUTE)?;
        Some(ResolvedInterpElf { file: interp_file, vmo: interp_vmo })
    } else {
        None
    };
    Ok(ResolvedElf { file: executable, vmo, interp, argv, environ })
}

/// Loads a resolved ELF into memory, along with an interpreter if one is defined, and initializes
/// the stack.
pub fn load_executable(
    current_task: &CurrentTask,
    resolved_elf: ResolvedElf,
) -> Result<ThreadStartInfo, Errno> {
    let main_elf = load_elf(resolved_elf.file, resolved_elf.vmo, &current_task.mm)?;
    let interp_elf = resolved_elf
        .interp
        .map(|interp| load_elf(interp.file, interp.vmo, &current_task.mm))
        .transpose()?;

    let entry_elf = interp_elf.as_ref().unwrap_or(&main_elf);
    let entry = UserAddress::from_ptr(
        entry_elf.headers.file_header().entry.wrapping_add(entry_elf.vaddr_bias),
    );

    let dt_debug_address = parse_debug_addr(&main_elf);

    // TODO(tbodt): implement MAP_GROWSDOWN and then reset this to 1 page. The current value of
    // this is based on adding 0x1000 each time a segfault appears.
    let stack_size: usize = 0xf0000;
    let stack_vmo = Arc::new(zx::Vmo::create(stack_size as u64).map_err(|_| errno!(ENOMEM))?);
    stack_vmo
        .as_ref()
        .set_name(CStr::from_bytes_with_nul(b"[stack]\0").unwrap())
        .map_err(impossible_error)?;
    let stack_base = current_task.mm.map(
        UserAddress::default(),
        Arc::clone(&stack_vmo),
        0,
        stack_size,
        zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
        MappingOptions::empty(),
        None,
    )?;
    let stack = stack_base + (stack_size - 8);

    let creds = current_task.creds.read();
    let auxv = vec![
        (AT_UID, creds.uid as u64),
        (AT_EUID, creds.euid as u64),
        (AT_GID, creds.gid as u64),
        (AT_EGID, creds.egid as u64),
        (AT_BASE, interp_elf.map_or(0, |interp| interp.file_base as u64)),
        (AT_PAGESZ, *PAGE_SIZE),
        (AT_PHDR, main_elf.file_base.wrapping_add(main_elf.headers.file_header().phoff) as u64),
        (AT_PHNUM, main_elf.headers.file_header().phnum as u64),
        (AT_ENTRY, main_elf.vaddr_bias.wrapping_add(main_elf.headers.file_header().entry) as u64),
        (AT_SECURE, 0),
    ];
    let stack = populate_initial_stack(
        &stack_vmo,
        &resolved_elf.argv,
        &resolved_elf.environ,
        auxv,
        stack_base,
        stack,
    )?;

    let mut mm_state = current_task.mm.state.write();
    mm_state.stack_base = stack_base;
    mm_state.stack_size = stack_size;

    Ok(ThreadStartInfo { entry, stack, dt_debug_address })
}

/// Parses the debug address (`DT_DEBUG`) from the provided ELF.
///
/// The debug address is read from the `elf_parse::SegmentType::Dynamic` program header,
/// if such a tag exists.
///
/// Returns `None` if no debug tag exists.
fn parse_debug_addr(elf: &LoadedElf) -> Option<UserAddress> {
    match elf.headers.program_header_with_type(elf_parse::SegmentType::Dynamic) {
        Ok(Some(dynamic_header)) => {
            const ENTRY_SIZE: usize = std::mem::size_of::<elf_parse::Elf64Dyn>();
            let mut header_bytes = vec![0u8; dynamic_header.filesz as usize];
            elf.vmo.read(&mut header_bytes, dynamic_header.offset as u64).ok()?;

            for offset in (0..(dynamic_header.filesz as usize)).step_by(ENTRY_SIZE) {
                let mut dyn_entry = elf_parse::Elf64Dyn::default();
                let entry_range = offset..(offset + ENTRY_SIZE);
                dyn_entry.as_bytes_mut().clone_from_slice(&header_bytes[entry_range]);

                if dyn_entry.tag() == Ok(elf_parse::Elf64DynTag::Debug) {
                    return Some(UserAddress::from(
                        elf.vaddr_bias.wrapping_add(dynamic_header.vaddr + offset) as u64,
                    ));
                }
            }

            None
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_trivial_initial_stack() {
        let stack_vmo = zx::Vmo::create(0x4000).expect("VMO creation should succeed.");
        let stack_base = UserAddress::from_ptr(0x3000_0000);
        let original_stack_start_addr = UserAddress::from_ptr(0x3000_1000);

        let argv = &vec![];
        let environ = &vec![];

        let stack_start_addr = populate_initial_stack(
            &stack_vmo,
            &argv,
            &environ,
            vec![],
            stack_base,
            original_stack_start_addr,
        )
        .expect("Populate initial stack should succeed.");

        let argc_size: usize = 8;
        let argv_terminator_size: usize = 8;
        let environ_terminator_size: usize = 8;
        let aux_random: usize = 16;
        let aux_null: usize = 16;
        let random_seed: usize = 16;

        let mut payload_size = argc_size
            + argv_terminator_size
            + environ_terminator_size
            + aux_random
            + aux_null
            + random_seed;
        payload_size += payload_size % 16;

        assert_eq!(stack_start_addr, original_stack_start_addr - payload_size);
    }

    fn exec_hello_starnix(current_task: &mut CurrentTask) -> Result<(), Errno> {
        let argv = vec![CString::new("bin/hello_starnix").unwrap()];
        current_task.exec(argv[0].clone(), argv, vec![])?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_hello_starnix() {
        let (_kernel, mut current_task) = create_kernel_and_task_with_pkgfs();
        exec_hello_starnix(&mut current_task).expect("failed to load executable");
        assert!(current_task.mm.get_mapping_count() > 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_snapshot_hello_starnix() {
        let (kernel, mut current_task) = create_kernel_and_task_with_pkgfs();
        exec_hello_starnix(&mut current_task).expect("failed to load executable");

        let current2 = create_task(&kernel, "another-task");
        current_task.mm.snapshot_to(&current2.mm).expect("failed to snapshot mm");

        assert_eq!(current_task.mm.get_mapping_count(), current2.mm.get_mapping_count());
    }
}
