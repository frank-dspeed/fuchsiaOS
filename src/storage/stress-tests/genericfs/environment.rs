// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        deletion_actor::DeletionActor, file_actor::FileActor, instance_actor::InstanceActor, Args,
    },
    async_trait::async_trait,
    either::Either,
    fidl_fuchsia_io as fio,
    fs_management::{filesystem::Filesystem, FSConfig},
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::Vmo,
    futures::lock::Mutex,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::path::PathBuf,
    std::sync::Arc,
    std::time::Duration,
    storage_stress_test_utils::{
        data::{Compressibility, FileFactory, UncompressedSize},
        fvm::{get_volume_path, FvmInstance, Guid},
        io::Directory,
    },
    stress_test::{actor::ActorRunner, environment::Environment, random_seed},
};

// All partitions in this test have their type set to this arbitrary GUID.
const TYPE_GUID: Guid =
    [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf];

const ONE_MIB: u64 = 1048576;
const FOUR_MIB: u64 = 4 * ONE_MIB;

const MOUNT_PATH: &str = "/fs";

pub fn open_dir_at_root(subdir: &str) -> Directory {
    let path = PathBuf::from(MOUNT_PATH).join(subdir);
    Directory::from_namespace(path, fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::RIGHT_READABLE)
        .unwrap()
}

/// Describes the environment that this stress test will run under.
pub struct FsEnvironment<FSC: FSConfig> {
    seed: u64,
    args: Args,
    vmo: Vmo,
    volume_guid: Guid,
    config: FSC,
    instance_actor: Arc<Mutex<InstanceActor>>,
    file_actor: Arc<Mutex<FileActor>>,
    deletion_actor: Arc<Mutex<DeletionActor>>,
}

impl<FSC: Clone + FSConfig> FsEnvironment<FSC> {
    pub async fn new(config: FSC, args: Args) -> Self {
        // Create the VMO that the ramdisk is backed by
        let vmo_size = args.ramdisk_block_count * args.ramdisk_block_size;
        let vmo = Vmo::create(vmo_size).unwrap();

        // Initialize the VMO with FVM partition style and a single filesystem partition

        // Create a ramdisk and setup FVM.
        let mut fvm =
            FvmInstance::new(true, &vmo, args.fvm_slice_size, args.ramdisk_block_size).await;

        // Initialize the filesystem on a new volume
        let volume_guid = fvm.new_volume("default", &TYPE_GUID, Some(fvm.free_space().await)).await;
        let volume_path = get_volume_path(&volume_guid).await;
        let node =
            connect_to_protocol_at_path::<fio::NodeMarker>(volume_path.to_str().unwrap()).unwrap();
        let mut fs = Filesystem::from_node(node, config.clone());
        fs.format().await.unwrap();

        let instance = if fs.config().is_multi_volume() {
            let mut instance = fs.serve_multi_volume().await.unwrap();
            let vol = instance.create_volume("default", None).await.unwrap();
            vol.bind_to_path(MOUNT_PATH).unwrap();
            Either::Right(instance)
        } else {
            let mut instance = fs.serve().await.unwrap();
            instance.bind_to_path(MOUNT_PATH).unwrap();
            Either::Left(instance)
        };

        let seed = match args.seed {
            Some(seed) => seed,
            None => random_seed(),
        };

        let mut rng = SmallRng::seed_from_u64(seed);

        // Make a home directory for file actor and deletion actor
        let root_dir = Directory::from_namespace(
            MOUNT_PATH,
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::RIGHT_READABLE,
        )
        .unwrap();
        root_dir
            .create_directory(
                "home1",
                fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::RIGHT_READABLE,
            )
            .await
            .unwrap();

        let file_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            let uncompressed_size = UncompressedSize::InRange(ONE_MIB, FOUR_MIB);
            let compressibility = Compressibility::Compressible;
            let factory = FileFactory::new(rng, uncompressed_size, compressibility);
            let home_dir = open_dir_at_root("home1");
            Arc::new(Mutex::new(FileActor::new(factory, home_dir)))
        };
        let deletion_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            let home_dir = open_dir_at_root("home1");
            Arc::new(Mutex::new(DeletionActor::new(rng, home_dir)))
        };

        let instance_actor = Arc::new(Mutex::new(InstanceActor::new(fvm, instance)));

        Self { seed, args, vmo, volume_guid, file_actor, deletion_actor, instance_actor, config }
    }
}

impl<FSC: FSConfig> std::fmt::Debug for FsEnvironment<FSC> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Environment").field("seed", &self.seed).field("args", &self.args).finish()
    }
}

#[async_trait]
impl<FSC: 'static + FSConfig + Clone + Send + Sync> Environment for FsEnvironment<FSC> {
    fn target_operations(&self) -> Option<u64> {
        self.args.num_operations
    }

    fn timeout_seconds(&self) -> Option<u64> {
        self.args.time_limit_secs
    }

    async fn actor_runners(&mut self) -> Vec<ActorRunner> {
        let mut runners = vec![
            ActorRunner::new("file_actor", None, self.file_actor.clone()),
            ActorRunner::new(
                "deletion_actor",
                Some(Duration::from_secs(5)),
                self.deletion_actor.clone(),
            ),
        ];

        if let Some(secs) = self.args.disconnect_secs {
            if secs > 0 {
                let runner = ActorRunner::new(
                    "instance_actor",
                    Some(Duration::from_secs(secs)),
                    self.instance_actor.clone(),
                );
                runners.push(runner);
            }
        }

        runners
    }

    async fn reset(&mut self) {
        {
            let mut actor = self.instance_actor.lock().await;

            // The environment is only reset when the instance is killed.
            // TODO(72385): Pass the actor error here, so it can be printed out on assert failure.
            assert!(actor.instance.is_none());

            // Create a ramdisk and setup FVM.
            let fvm = FvmInstance::new(
                false,
                &self.vmo,
                self.args.fvm_slice_size,
                self.args.ramdisk_block_size,
            )
            .await;
            let volume_path = get_volume_path(&self.volume_guid).await;
            let node =
                connect_to_protocol_at_path::<fio::NodeMarker>(volume_path.to_str().unwrap())
                    .unwrap();

            let mut fs = Filesystem::from_node(node, self.config.clone());
            fs.fsck().await.unwrap();
            let instance = if fs.config().is_multi_volume() {
                let mut instance = fs.serve_multi_volume().await.unwrap();
                let vol = instance.open_volume("default", None).await.unwrap();
                vol.bind_to_path(MOUNT_PATH).unwrap();
                Either::Right(instance)
            } else {
                let mut instance = fs.serve().await.unwrap();
                instance.bind_to_path(MOUNT_PATH).unwrap();
                Either::Left(instance)
            };

            // Replace the fvm and fs instances
            actor.instance = Some((fvm, instance));
        }

        // Replace the root directory with a new one
        {
            let mut actor = self.file_actor.lock().await;
            actor.home_dir = open_dir_at_root("home1");
        }

        {
            let mut actor = self.deletion_actor.lock().await;
            actor.home_dir = open_dir_at_root("home1");
        }
    }
}
