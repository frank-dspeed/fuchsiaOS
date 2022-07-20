// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystem-mounter.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.fxfs/cpp/wire_types.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <sys/stat.h>
#include <zircon/status.h>

#include <string>

#include <fbl/ref_ptr.h>

#include "constants.h"
#include "fdio.h"
#include "fidl/fuchsia.io/cpp/markers.h"
#include "lib/fdio/namespace.h"
#include "lib/fidl/cpp/wire/internal/transport_channel.h"
#include "lib/zx/status.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/fs_management/cpp/options.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/minfs/minfs.h"

namespace fshost {

namespace fio = fuchsia_io;

constexpr unsigned char kInsecureCryptDataKey[32] = {
    0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,  0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

constexpr unsigned char kInsecureCryptMetadataKey[32] = {
    0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0,
    0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8, 0xe7, 0xe6, 0xe5, 0xe4, 0xe3, 0xe2, 0xe1, 0xe0,
};

zx::status<> FilesystemMounter::LaunchFsComponent(zx::channel block_device,
                                                  const fs_management::MountOptions& options,
                                                  const std::string& partition_name) {
  std::string startup_service_path =
      std::string("/") + partition_name + "/fuchsia.fs.startup.Startup";
  auto startup_client_end =
      service::Connect<fuchsia_fs_startup::Startup>(startup_service_path.c_str());
  if (startup_client_end.is_error()) {
    FX_LOGS(ERROR) << "failed to connect to startup service at " << startup_service_path << ": "
                   << startup_client_end.status_string();
    return startup_client_end.take_error();
  }
  auto startup_client = fidl::BindSyncClient(std::move(*startup_client_end));
  fidl::ClientEnd<fuchsia_hardware_block::Block> block_client_end(std::move(block_device));
  auto start_options_or = options.as_start_options();
  if (!start_options_or.is_ok()) {
    FX_PLOGS(ERROR, start_options_or.error_value()) << "Failed to build start options";
    return start_options_or.take_error();
  }
  auto startup_res =
      startup_client->Start(std::move(block_client_end), std::move(start_options_or.value()));
  if (!startup_res.ok()) {
    FX_LOGS(ERROR) << "failed to start through startup service at " << startup_service_path << ": "
                   << startup_res.status_string();
    return zx::make_status(startup_res.status());
  }
  FX_LOGS(INFO) << "successfully mounted " << partition_name;
  return zx::ok();
}

zx_status_t FilesystemMounter::LaunchFs(int argc, const char** argv, zx_handle_t* hnd,
                                        uint32_t* ids, size_t len) {
  return Launch(*zx::job::default_job(), argv[0], argv, nullptr, -1,
                /* TODO(fxbug.dev/32044) */ zx::resource(), hnd, ids, len, nullptr);
}

zx::status<std::string> FilesystemMounter::GetDevicePath(const zx::channel& block_device) {
  std::string device_path;
  if (auto result =
          fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(block_device.borrow()))
              ->GetTopologicalPath();
      result.status() != ZX_OK) {
    FX_LOGS(WARNING) << "Unable to get device topological path (FIDL error): "
                     << zx_status_get_string(result.status());
    return zx::error(result.status());
  } else if (result->is_error()) {
    FX_LOGS(WARNING) << "Unable to get device topological path: "
                     << zx_status_get_string(result->error_value());
    return zx::error(result->error_value());
  } else {
    device_path = result->value()->path.get();
  }
  return zx::ok(device_path);
}

zx::status<> FilesystemMounter::MountFilesystem(FsManager::MountPoint point, const char* binary,
                                                const fs_management::MountOptions& options,
                                                zx::channel block_device_client,
                                                fidl::ClientEnd<fuchsia_fxfs::Crypt> crypt_client) {
  FX_LOGS(INFO) << "FilesystemMounter::MountFilesystem(" << binary << ")";
  std::string device_path = GetDevicePath(block_device_client).value_or("");
  FX_LOGS(INFO) << "Mounting device " << device_path << " with " << binary << " at "
                << FsManager::MountPointPath(point);

  std::optional endpoints_or = fshost_.TakeMountPointServerEnd(point);
  if (!endpoints_or.has_value()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  auto [export_root, server_end] = std::move(endpoints_or.value());

  size_t num_handles = crypt_client ? 3 : 2;
  zx_handle_t handles[] = {server_end.TakeChannel().release(), block_device_client.release(),
                           crypt_client.TakeChannel().release()};
  uint32_t ids[] = {PA_DIRECTORY_REQUEST, FS_HANDLE_BLOCK_DEVICE_ID, PA_HND(PA_USER0, 2)};

  fbl::Vector<const char*> argv;
  argv.push_back(binary);
  if (options.readonly) {
    argv.push_back("--readonly");
  }
  if (options.verbose_mount) {
    argv.push_back("--verbose");
  }
  if (options.write_compression_algorithm) {
    argv.push_back("--compression");
    argv.push_back(options.write_compression_algorithm->c_str());
  }
  if (options.sandbox_decompression) {
    argv.push_back("--sandbox_decompression");
  }
  if (options.cache_eviction_policy) {
    argv.push_back("--eviction_policy");
    argv.push_back(options.cache_eviction_policy->c_str());
  }
  argv.push_back("mount");
  argv.push_back(nullptr);
  if (zx_status_t status =
          LaunchFs(static_cast<int>(argv.size() - 1), argv.data(), handles, ids, num_handles);
      status != ZX_OK) {
    return zx::error(status);
  }

  auto result = fidl::WireCall(export_root)->Describe();
  if (!result.ok()) {
    return zx::error(result.status());
  }

  fshost_.RegisterDevicePath(point, device_path);

  return zx::ok();
}

zx_status_t FilesystemMounter::MountData(zx::channel block_device,
                                         fs_management::MountOptions options,
                                         fs_management::DiskFormat format) {
  if (data_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (format == fs_management::kDiskFormatF2fs) {
    std::string binary_path(BinaryPathForFormat(format));
    if (binary_path.empty()) {
      FX_LOGS(ERROR) << "Unsupported format: " << DiskFormatString(format);
      return ZX_ERR_INVALID_ARGS;
    }

    if (auto result = MountFilesystem(FsManager::MountPoint::kData, binary_path.c_str(), options,
                                      std::move(block_device), {});
        result.is_error()) {
      return result.error_value();
    }

    if (zx_status_t status = fshost_.ForwardFsDiagnosticsDirectory(
            FsManager::MountPoint::kData, fs_management::DiskFormatString(format));
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to add diagnostic directory for "
                              << fs_management::DiskFormatString(format);
    }
  } else {
    // Note: filesystem-mounter-test.cc stubs out LaunchFsComponent and passes in invalid channels.
    // GetDevicePath are ignored and errors benign in these tests.
    const std::string device_path = GetDevicePath(block_device).value_or("");

    if (format == fs_management::kDiskFormatFxfs) {
      options.crypt_client = []() {
        // TODO(fxbug.dev/99591): Statically route the crypt service.
        auto crypt_client_or = service::Connect<fuchsia_fxfs::Crypt>();
        if (crypt_client_or.is_error()) {
          FX_PLOGS(ERROR, crypt_client_or.error_value()) << "Failed to connect to Crypt service.";
          return zx::channel();
        }
        return std::move(crypt_client_or).value().TakeChannel();
      };
    }

    auto status = LaunchFsComponent(std::move(block_device), options, "data");
    if (status.is_error()) {
      FX_PLOGS(ERROR, status.error_value()) << "Failed to launch filesystem component.";
      return status.error_value();
    }

    fshost_.RegisterDevicePath(FsManager::MountPoint::kData, device_path);
  }

  // Obtain data root used for serving disk usage statistics. Must be valid otherwise the lazy node
  // serving the statistics will hang indefinitely. If we fail to get the data root, just log
  // loudly, but don't fail. Serving stats is best-effort and shouldn't cause mounting to fail.
  auto data_root_or = manager().GetRoot(FsManager::MountPoint::kData);
  if (data_root_or.is_ok()) {
    inspect_manager().ServeStats("data", std::move(data_root_or.value()));
  } else {
    FX_LOGS(WARNING) << "failed to get data root to serve inspect stats. Assuming test environment "
                        "and continuing.";
  }

  data_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountFactoryFs(zx::channel block_device,
                                              const fs_management::MountOptions& options) {
  if (factory_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (auto result = MountFilesystem(FsManager::MountPoint::kFactory, kFactoryfsPath, options,
                                    std::move(block_device));
      result.is_error()) {
    return result.error_value();
  }

  factory_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountDurable(zx::channel block_device,
                                            const fs_management::MountOptions& options) {
  if (durable_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (auto result = MountFilesystem(FsManager::MountPoint::kDurable, kMinfsPath, options,
                                    std::move(block_device));
      result.is_error()) {
    return result.error_value();
  }

  durable_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountBlob(zx::channel block_device,
                                         const fs_management::MountOptions& options) {
  if (blob_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  zx::status ret = LaunchFsComponent(std::move(block_device), std::move(options), "blobfs");
  if (ret.is_error()) {
    return ret.error_value();
  }

  blob_mounted_ = true;
  return ZX_OK;
}

void FilesystemMounter::ReportPartitionCorrupted(fs_management::DiskFormat format) {
  fshost_.inspect_manager().LogCorruption(format);
  // Currently the only reason we report a partition as being corrupt is if it fails fsck.
  // This may need to change in the future should we want to file synthetic crash reports for
  // other possible failure modes.
  fshost_.FileReport(format, FsManager::ReportReason::kFsckFailure);
}

zx::status<> FilesystemMounter::MaybeInitCryptClient() {
  if (config_.data_filesystem_format() != "fxfs") {
    FX_LOGS(INFO) << "Not initializing Crypt client due to configuration";
    return zx::ok();
  }
  FX_LOGS(INFO) << "Initializing Crypt client";
  auto management_endpoints_or = fidl::CreateEndpoints<fuchsia_fxfs::CryptManagement>();
  if (management_endpoints_or.is_error())
    return zx::error(management_endpoints_or.status_value());
  if (zx_status_t status =
          fdio_service_connect(fidl::DiscoverableProtocolDefaultPath<fuchsia_fxfs::CryptManagement>,
                               management_endpoints_or->server.TakeChannel().release());
      status != ZX_OK) {
    return zx::error(status);
  }
  auto client = fidl::BindSyncClient(std::move(management_endpoints_or->client));
  // TODO(fxbug.dev/94587): A hardware source should be used for keys.
  unsigned char key0[32] = {0};
  unsigned char key1[32] = {0};
  std::copy(std::begin(kInsecureCryptDataKey), std::end(kInsecureCryptDataKey), key0);
  std::copy(std::begin(kInsecureCryptMetadataKey), std::end(kInsecureCryptMetadataKey), key1);
  if (auto result = client->AddWrappingKey(0, fidl::VectorView<unsigned char>::FromExternal(key0));
      !result.ok()) {
    FX_LOGS(ERROR) << "Failed to add wrapping key: " << zx_status_get_string(result.status());
    return zx::error(result.status());
  }
  if (auto result = client->AddWrappingKey(1, fidl::VectorView<unsigned char>::FromExternal(key1));
      !result.ok()) {
    FX_LOGS(ERROR) << "Failed to add wrapping key: " << zx_status_get_string(result.status());
    return zx::error(result.status());
  }
  if (auto result = client->SetActiveKey(fuchsia_fxfs::wire::KeyPurpose::kData, 0); !result.ok()) {
    FX_LOGS(ERROR) << "Failed to set active data key: " << zx_status_get_string(result.status());
    return zx::error(result.status());
  }
  if (auto result = client->SetActiveKey(fuchsia_fxfs::wire::KeyPurpose::kMetadata, 1);
      !result.ok()) {
    FX_LOGS(ERROR) << "Failed to set active metadata key: "
                   << zx_status_get_string(result.status());
    return zx::error(result.status());
  }
  return zx::ok();
}

std::string_view BinaryPathForFormat(fs_management::DiskFormat format) {
  switch (format) {
    case fs_management::kDiskFormatFxfs:
      return kFxfsPath;
    case fs_management::kDiskFormatF2fs:
      return kF2fsPath;
    case fs_management::kDiskFormatMinfs:
      return kMinfsPath;
    default:
      return "";
  }
}

}  // namespace fshost
