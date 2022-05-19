// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use fuchsia_zircon::{self as zx, AsHandleRef};
use magma::*;
use zerocopy::{AsBytes, FromBytes};

use crate::fs::{anon_fs, Anon, FdFlags, VmoFileObject};
use crate::task::CurrentTask;
use crate::types::*;

/// Returns a vector of at least one `T`. The vector will be of length `item_count` if > 0.
fn at_least_one<T>(item_count: usize) -> Vec<T>
where
    T: Default + Clone,
{
    vec![T::default(); std::cmp::max(1, item_count)]
}

/// Reads a sequence of objects starting at `addr`.
///
/// # Parameters
///   - `current_task`: The task from which to read the objects.
///   - `addr`: The address of the first item to read.
///   - `item_count`: The number of items to read. If 0, a 1-item vector will be returned to make
///                   sure that the calling code can safely pass `&mut vec[0]` to libmagma.
fn read_objects<T>(
    current_task: &CurrentTask,
    addr: UserAddress,
    item_count: usize,
) -> Result<Vec<T>, Errno>
where
    T: Default + Clone + FromBytes,
{
    let mut items = at_least_one::<T>(item_count);
    if item_count > 0 {
        let user_ref: UserRef<T> = addr.into();
        current_task.mm.read_objects(user_ref, &mut items)?;
    }
    Ok(items)
}

/// Imports a device to magma.
///
/// # Parameters
///   - `control`: The control struct containing the device channel to import from.
///   - `response`: The struct that will be filled out to contain the response. This struct can be
///                 written back to userspace.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.
pub fn device_import(
    _control: virtio_magma_device_import_ctrl_t,
    response: &mut virtio_magma_device_import_resp_t,
) -> Result<zx::Channel, Errno> {
    let (client_channel, server_channel) = zx::Channel::create().map_err(|_| errno!(EINVAL))?;
    // TODO(fxbug.dev/100454): This currently picks the first available device, but multiple devices should
    // probably be exposed to clients.
    let entry = std::fs::read_dir("/dev/class/gpu")
        .map_err(|_| errno!(EINVAL))?
        .next()
        .ok_or(errno!(EINVAL))?
        .map_err(|_| errno!(EINVAL))?;

    let path = entry.path().into_os_string().into_string().map_err(|_| errno!(EINVAL))?;

    fdio::service_connect(&path, server_channel).map_err(|_| errno!(EINVAL))?;

    // TODO(fxbug.dev/12731): The device import should take ownership of the channel, at which point
    // this can be converted to `into_raw()`, and the return value of this function can be changed
    // to be `()`.
    let device_channel = client_channel.raw_handle();

    let mut device_out: u64 = 0;
    response.result_return =
        unsafe { magma_device_import(device_channel, &mut device_out as *mut u64) as u64 };

    response.device_out = device_out;

    Ok(client_channel)
}

/// `WireDescriptor` matches the struct used by libmagma_linux to encode some fields of the magma
/// command descriptor.
#[repr(C)]
#[derive(FromBytes, AsBytes, Default, Debug)]
struct WireDescriptor {
    resource_count: u32,
    command_buffer_count: u32,
    wait_semaphore_count: u32,
    signal_semaphore_count: u32,
    flags: u64,
}

/// Executes a magma command.
///
/// This function bridges between the virtmagma structs and the magma structures. It also copies the
/// data into starnix in order to be able to pass pointers to the resources, command buffers, and
/// semaphore ids to magma.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.
pub fn execute_command(
    current_task: &CurrentTask,
    control: virtio_magma_execute_command_ctrl_t,
    response: &mut virtio_magma_execute_command_resp_t,
) -> Result<(), Errno> {
    let virtmagma_command_descriptor_addr =
        UserRef::<virtmagma_command_descriptor>::new(control.descriptor.into());
    let mut command_descriptor = virtmagma_command_descriptor::default();
    current_task.mm.read_object(virtmagma_command_descriptor_addr, &mut command_descriptor)?;

    // Read the virtmagma-internal struct that contains the counts and flags for the magma command
    // descriptor.
    let mut wire_descriptor = WireDescriptor::default();
    current_task.mm.read_object(
        UserAddress::from(command_descriptor.descriptor).into(),
        &mut wire_descriptor,
    )?;

    // This is the command descriptor that will be populated from the virtmagma
    // descriptor and subsequently passed to magma_execute_command.
    let mut magma_command_descriptor = magma_command_descriptor::default();
    magma_command_descriptor.resource_count = wire_descriptor.resource_count;
    magma_command_descriptor.command_buffer_count = wire_descriptor.command_buffer_count;
    magma_command_descriptor.wait_semaphore_count = wire_descriptor.wait_semaphore_count;
    magma_command_descriptor.signal_semaphore_count = wire_descriptor.signal_semaphore_count;
    magma_command_descriptor.flags = wire_descriptor.flags;
    let semaphore_count =
        (wire_descriptor.wait_semaphore_count + wire_descriptor.signal_semaphore_count) as usize;

    // Read all the passed in resources, commands, and semaphore ids.
    let mut resources: Vec<magma_exec_resource> = read_objects(
        current_task,
        command_descriptor.resources.into(),
        wire_descriptor.resource_count as usize,
    )?;
    let mut command_buffers: Vec<magma_exec_command_buffer> = read_objects(
        current_task,
        command_descriptor.command_buffers.into(),
        wire_descriptor.command_buffer_count as usize,
    )?;
    let mut semaphores: Vec<u64> =
        read_objects(current_task, command_descriptor.semaphores.into(), semaphore_count)?;

    // Make sure the command descriptor contains valid pointers for the resources, command buffers,
    // and semaphore ids.
    magma_command_descriptor.resources = &mut resources[0] as *mut magma_exec_resource;
    magma_command_descriptor.command_buffers =
        &mut command_buffers[0] as *mut magma_exec_command_buffer;
    magma_command_descriptor.semaphore_ids = &mut semaphores[0] as *mut u64;

    response.result_return = unsafe {
        magma_execute_command(
            control.connection,
            control.context_id,
            &mut magma_command_descriptor as *mut magma_command_descriptor,
        ) as u64
    };

    Ok(())
}

/// Runs a magma query.
///
/// This function will create a new file in `current_task.files` if the magma query returns a VMO
/// handle. The file takes ownership of the VMO handle, and the file descriptor of the file is
/// returned to the client via `response`.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.
pub fn query(
    current_task: &CurrentTask,
    control: virtio_magma_query_ctrl_t,
    response: &mut virtio_magma_query_resp_t,
) -> Result<(), Errno> {
    let mut result_buffer_out = 0;
    let mut result_out = 0;
    response.result_return = unsafe {
        magma_query(control.device, control.id, &mut result_buffer_out, &mut result_out) as u64
    };

    if result_buffer_out != zx::sys::ZX_HANDLE_INVALID {
        let vmo = unsafe { zx::Vmo::from(zx::Handle::from_raw(result_buffer_out)) };
        let file = Anon::new_file(
            anon_fs(current_task.kernel()),
            Box::new(VmoFileObject::new(Arc::new(vmo))),
            OpenFlags::RDWR,
        );
        let fd = current_task.files.add_with_flags(file, FdFlags::empty())?;
        response.result_buffer_out = fd.raw() as u64;
    } else {
        response.result_buffer_out = u64::MAX;
    }

    response.result_out = result_out;

    Ok(())
}
