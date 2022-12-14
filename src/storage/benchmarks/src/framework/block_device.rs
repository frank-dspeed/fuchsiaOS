// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_hardware_block_volume::ALLOCATE_PARTITION_FLAG_INACTIVE,
    fidl_fuchsia_hardware_block_volume::{
        VolumeManagerMarker, VolumeManagerProxy, VolumeSynchronousProxy,
    },
    fidl_fuchsia_io as fio,
    fuchsia_component::client::{connect_channel_to_protocol_at_path, connect_to_protocol_at_path},
    fuchsia_zircon::{self as zx},
    ramdevice_client::RamdiskClient,
    std::path::{Path, PathBuf},
    storage_isolated_driver_manager::{
        create_random_guid, fvm, wait_for_block_device, wait_for_ramctl, zxcrypt,
        BlockDeviceMatcher, Guid,
    },
};

const RAMDISK_FVM_SLICE_SIZE: usize = 1024 * 1024;

// From zircon/system/public/zircon/hw/gpt.h.
const BLOBFS_TYPE_GUID: &Guid = &[
    0x0e, 0x38, 0x67, 0x29, 0x4c, 0x13, 0xbb, 0x4c, 0xb6, 0xda, 0x17, 0xe7, 0xce, 0x1c, 0xa4, 0x5d,
];
const BLOBFS_VOLUME_NAME: &str = "blobfs";

/// Block device configuration options.
pub struct BlockDeviceConfig {
    /// If true, zxcrypt is initialized on top of the block device.
    pub use_zxcrypt: bool,

    /// For filesystem that are not FVM-aware, this option can be used to pre-allocate space inside
    /// of the FVM volume.
    pub fvm_volume_size: Option<u64>,
}

/// A trait representing a block device.
pub trait BlockDevice {
    /// Returns a `NodeProxy` to the block device.
    fn get_node(&self) -> fio::NodeProxy;
}

/// A trait for constructing block devices.
#[async_trait]
pub trait BlockDeviceFactory {
    type BlockDevice: BlockDevice + Send + 'static;

    /// Constructs a new block device.
    async fn create_block_device(&self, config: &BlockDeviceConfig) -> Self::BlockDevice;
}

/// Creates block devices on ramdisks.
pub struct RamdiskFactory {
    block_size: u64,
    block_count: u64,
}

impl RamdiskFactory {
    #[allow(dead_code)]
    pub async fn new(block_size: u64, block_count: u64) -> Self {
        wait_for_ramctl().await.expect("ramctl did not appear");
        Self { block_size, block_count }
    }
}

#[async_trait]
impl BlockDeviceFactory for RamdiskFactory {
    type BlockDevice = Ramdisk;
    async fn create_block_device(&self, config: &BlockDeviceConfig) -> Ramdisk {
        Ramdisk::new(self.block_size, self.block_count, config).await
    }
}

/// A ramdisk backed block device.
pub struct Ramdisk {
    _ramdisk: RamdiskClient,
    node: fio::NodeProxy,
}

impl Ramdisk {
    async fn new(block_size: u64, block_count: u64, config: &BlockDeviceConfig) -> Self {
        let ramdisk =
            RamdiskClient::create(block_size, block_count).expect("Failed to create RamdiskClient");

        let volume_manager = fvm::set_up_fvm(Path::new(ramdisk.get_path()), RAMDISK_FVM_SLICE_SIZE)
            .await
            .expect("Failed to set up FVM");
        let volume_path = set_up_fvm_volume(&volume_manager, config.fvm_volume_size).await;

        let path = if config.use_zxcrypt {
            zxcrypt::set_up_insecure_zxcrypt(&volume_path).await.expect("Failed to set up zxcrypt")
        } else {
            volume_path
        };
        let node = connect_to_protocol_at_path::<fio::NodeMarker>(path.to_str().unwrap()).unwrap();

        Self { _ramdisk: ramdisk, node }
    }
}

impl BlockDevice for Ramdisk {
    fn get_node(&self) -> fio::NodeProxy {
        Clone::clone(&self.node)
    }
}

/// Creates block devices on top of the system's FVM instance.
pub struct FvmVolumeFactory {
    fvm: VolumeManagerProxy,
}

impl FvmVolumeFactory {
    pub async fn new() -> Self {
        // Find Blobfs' volume then work backwards from Blobfs' topological path to find FVM.
        let blobfs_dev_path = wait_for_block_device(&[
            BlockDeviceMatcher::Name(BLOBFS_VOLUME_NAME),
            BlockDeviceMatcher::TypeGuid(BLOBFS_TYPE_GUID),
        ])
        .await
        .expect("Failed to find Blobfs");

        let blobfs_controller =
            connect_to_protocol_at_path::<ControllerMarker>(blobfs_dev_path.to_str().unwrap())
                .unwrap();
        let path = blobfs_controller
            .get_topological_path()
            .await
            .expect("Failed to get topological path")
            .map_err(zx::Status::from_raw)
            .expect("Failed to get topological path");

        let mut path = PathBuf::from(path);
        if !path.pop() || !path.pop() {
            panic!("Unexpected topological path for Blobfs {}", path.display());
        }

        match path.file_name() {
            Some(p) => assert!(p == "fvm", "Unexpected FVM path: {}", path.display()),
            None => panic!("Unexpected FVM path: {}", path.display()),
        }
        let fvm =
            connect_to_protocol_at_path::<VolumeManagerMarker>(path.to_str().unwrap()).unwrap();

        Self { fvm }
    }
}

#[async_trait]
impl BlockDeviceFactory for FvmVolumeFactory {
    type BlockDevice = FvmVolume;
    async fn create_block_device(&self, config: &BlockDeviceConfig) -> FvmVolume {
        FvmVolume::new(&self.fvm, config).await
    }
}

/// A block device created on top of the system's FVM instance.
pub struct FvmVolume {
    volume: VolumeSynchronousProxy,
    node: fio::NodeProxy,
}

impl FvmVolume {
    async fn new(fvm: &VolumeManagerProxy, config: &BlockDeviceConfig) -> Self {
        let volume_path = set_up_fvm_volume(fvm, config.fvm_volume_size).await;
        let (client_end, server_end) = zx::Channel::create().unwrap();
        connect_channel_to_protocol_at_path(server_end, volume_path.to_str().unwrap()).unwrap();
        let volume = VolumeSynchronousProxy::new(client_end);

        let path = if config.use_zxcrypt {
            zxcrypt::set_up_insecure_zxcrypt(&volume_path).await.expect("Failed to set up zxcrypt")
        } else {
            volume_path
        };
        let node = connect_to_protocol_at_path::<fio::NodeMarker>(path.to_str().unwrap()).unwrap();

        Self { volume, node }
    }
}

impl BlockDevice for FvmVolume {
    fn get_node(&self) -> fio::NodeProxy {
        Clone::clone(&self.node)
    }
}

impl Drop for FvmVolume {
    fn drop(&mut self) {
        let status =
            self.volume.destroy(zx::Time::INFINITE).expect("Failed to destroy the FVM volume");
        zx::ok(status).expect("Failed to destroy the FVM volume");
    }
}

async fn set_up_fvm_volume(
    volume_manager: &VolumeManagerProxy,
    volume_size: Option<u64>,
) -> PathBuf {
    const BENCHMARK_TYPE_GUID: &Guid = &[
        0x67, 0x45, 0x23, 0x01, 0xab, 0x89, 0xef, 0xcd, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd,
        0xef,
    ];
    const BENCHMARK_VOLUME_NAME: &str = "benchmark";

    let instance_guid = create_random_guid();
    fvm::create_fvm_volume(
        volume_manager,
        BENCHMARK_VOLUME_NAME,
        BENCHMARK_TYPE_GUID,
        &instance_guid,
        volume_size,
        ALLOCATE_PARTITION_FLAG_INACTIVE,
    )
    .await
    .expect("Failed to create FVM volume");

    let device_path = wait_for_block_device(&[
        BlockDeviceMatcher::TypeGuid(BENCHMARK_TYPE_GUID),
        BlockDeviceMatcher::InstanceGuid(&instance_guid),
        BlockDeviceMatcher::Name(BENCHMARK_VOLUME_NAME),
    ])
    .await
    .expect("Failed to find the FVM volume");

    let controller =
        connect_to_protocol_at_path::<ControllerMarker>(device_path.to_str().unwrap()).unwrap();
    let topological_path = controller
        .get_topological_path()
        .await
        .expect("Failed to get topological path")
        .map_err(zx::Status::from_raw)
        .expect("Failed to get topological path");
    PathBuf::from(topological_path)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_endpoints, ProtocolMarker, Proxy},
        fidl_fuchsia_hardware_block_volume::VolumeMarker,
        test_util::assert_gt,
    };

    const BLOCK_SIZE: u64 = 4 * 1024;
    const BLOCK_COUNT: u64 = 1024;

    fn open_connection_as<T: ProtocolMarker>(ramdisk: &impl BlockDevice) -> T::Proxy {
        let node = ramdisk.get_node();
        let (client_end, server_end) = create_endpoints().unwrap();
        node.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server_end).unwrap();
        <T as ProtocolMarker>::Proxy::from_channel(
            fidl::AsyncChannel::from_channel(client_end.into_channel()).unwrap(),
        )
    }

    #[fuchsia::test]
    async fn ramdisk_create_block_device_with_zxcrypt() {
        let ramdisk_factory = RamdiskFactory::new(BLOCK_SIZE, BLOCK_COUNT).await;
        let ramdisk = ramdisk_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: true, fvm_volume_size: None })
            .await;
        let controller = open_connection_as::<ControllerMarker>(&ramdisk);
        let path = controller
            .get_topological_path()
            .await
            .expect("Failed to get topological path")
            .map_err(zx::Status::from_raw)
            .expect("Failed to get topological path");
        assert!(path.contains("/zxcrypt/"), "block device path does not contain zxcrypt: {}", path);
    }

    #[fuchsia::test]
    async fn ramdisk_create_block_device_without_zxcrypt() {
        let ramdisk_factory = RamdiskFactory::new(BLOCK_SIZE, BLOCK_COUNT).await;
        let ramdisk = ramdisk_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: false, fvm_volume_size: None })
            .await;
        let controller = open_connection_as::<ControllerMarker>(&ramdisk);
        let path = controller
            .get_topological_path()
            .await
            .expect("Failed to get topological path")
            .map_err(zx::Status::from_raw)
            .expect("Failed to get topological path");
        assert!(
            !path.contains("/zxcrypt/"),
            "block device path should not contain zxcrypt: {}",
            path
        );
    }

    #[fuchsia::test]
    async fn ramdisk_create_block_device_without_volume_size() {
        let ramdisk_factory = RamdiskFactory::new(BLOCK_SIZE, BLOCK_COUNT).await;
        let ramdisk = ramdisk_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: false, fvm_volume_size: None })
            .await;
        let volume = open_connection_as::<VolumeMarker>(&ramdisk);
        let volume_info = volume.get_volume_info().await.unwrap();
        zx::ok(volume_info.0).unwrap();
        let volume_info = volume_info.2.unwrap();
        assert_eq!(volume_info.partition_slice_count, 1);
    }

    #[fuchsia::test]
    async fn ramdisk_create_block_device_with_volume_size() {
        let ramdisk_factory = RamdiskFactory::new(BLOCK_SIZE, BLOCK_COUNT).await;
        let ramdisk = ramdisk_factory
            .create_block_device(&BlockDeviceConfig {
                use_zxcrypt: false,
                fvm_volume_size: Some(RAMDISK_FVM_SLICE_SIZE as u64 * 3),
            })
            .await;
        let volume = open_connection_as::<VolumeMarker>(&ramdisk);
        let volume_info = volume.get_volume_info().await.unwrap();
        zx::ok(volume_info.0).unwrap();
        let volume_info = volume_info.2.unwrap();
        assert_eq!(volume_info.partition_slice_count, 3);
    }

    async fn init_ramdisk_for_fvm_volume_factory() -> RamdiskClient {
        // The tests are run in an isolated devmgr which doesn't have access to the real FVM or
        // blobfs. Create a ramdisk, set up fvm, and add a blobfs volume for `FvmVolumeFactory` to
        // find.
        wait_for_ramctl().await.expect("ramctl did not appear");
        let ramdisk_client =
            RamdiskClient::create(BLOCK_SIZE, BLOCK_COUNT).expect("Failed to create RamdiskClient");
        let volume_manager =
            fvm::set_up_fvm(Path::new(ramdisk_client.get_path()), RAMDISK_FVM_SLICE_SIZE)
                .await
                .expect("Failed to set up FVM");
        fvm::create_fvm_volume(
            &volume_manager,
            BLOBFS_VOLUME_NAME,
            BLOBFS_TYPE_GUID,
            &create_random_guid(),
            None,
            ALLOCATE_PARTITION_FLAG_INACTIVE,
        )
        .await
        .expect("Failed to create blobfs");
        ramdisk_client
    }

    #[fuchsia::test]
    async fn fvm_volume_factory_can_find_fvm_instance() {
        let _ramdisk = init_ramdisk_for_fvm_volume_factory().await;
        let volume_factory = FvmVolumeFactory::new().await;

        // Verify that a volume can be created.
        volume_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: false, fvm_volume_size: None })
            .await;
    }

    async fn get_fvm_used_slices(fvm: &VolumeManagerProxy) -> u64 {
        let info = fvm.get_info().await.unwrap();
        zx::ok(info.0).unwrap();
        let info = info.1.unwrap();
        info.assigned_slice_count
    }

    #[fuchsia::test]
    async fn dropping_an_fvm_volume_removes_the_volume() {
        let _ramdisk = init_ramdisk_for_fvm_volume_factory().await;
        let volume_factory = FvmVolumeFactory::new().await;
        let used_slices = get_fvm_used_slices(&volume_factory.fvm).await;

        let volume = volume_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: false, fvm_volume_size: None })
            .await;
        // The number of used slices should have gone up.
        assert_gt!(get_fvm_used_slices(&volume_factory.fvm).await, used_slices);

        std::mem::drop(volume);
        // The number of used slices should have gone back down.
        assert_eq!(get_fvm_used_slices(&volume_factory.fvm).await, used_slices);
    }

    #[fuchsia::test]
    async fn fvm_volume_factory_create_block_device_with_zxcrypt() {
        let _ramdisk = init_ramdisk_for_fvm_volume_factory().await;
        let volume_factory = FvmVolumeFactory::new().await;
        let volume = volume_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: true, fvm_volume_size: None })
            .await;

        let controller = open_connection_as::<ControllerMarker>(&volume);
        let path = controller
            .get_topological_path()
            .await
            .expect("Failed to get topological path")
            .map_err(zx::Status::from_raw)
            .expect("Failed to get topological path");
        assert!(path.contains("/zxcrypt/"), "block device path does not contain zxcrypt: {}", path);
    }

    #[fuchsia::test]
    async fn fvm_volume_factory_create_block_device_without_zxcrypt() {
        let _ramdisk = init_ramdisk_for_fvm_volume_factory().await;
        let volume_factory = FvmVolumeFactory::new().await;
        let volume = volume_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: false, fvm_volume_size: None })
            .await;

        let controller = open_connection_as::<ControllerMarker>(&volume);
        let path = controller
            .get_topological_path()
            .await
            .expect("Failed to get topological path")
            .map_err(zx::Status::from_raw)
            .expect("Failed to get topological path");
        assert!(
            !path.contains("/zxcrypt/"),
            "block device path should not contain zxcrypt: {}",
            path
        );
    }
}
