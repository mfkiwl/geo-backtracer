#include <glog/logging.h>
#include <grpc++/grpc++.h>

#include "common/signal.h"
#include "server/mixer.h"

namespace bt {

ShardHandler::ShardHandler(const ShardConfig &config) : config_(config) {}

Status ShardHandler::Init() {
  for (const auto &worker : config_.workers_) {
    stubs_.push_back(proto::Pusher::NewStub(
        grpc::CreateChannel(worker, grpc::InsecureChannelCredentials())));
  }

  return StatusCode::OK;
}

const std::string &ShardHandler::Name() const { return config_.name_; }

grpc::Status ShardHandler::DeleteUser(const proto::DeleteUserRequest *request,
                                      proto::DeleteUserResponse *response) {
  for (auto &stub : stubs_) {
    grpc::ClientContext context;
    grpc::Status status = stub->DeleteUser(&context, *request, response);
    if (!status.ok()) {
      return status;
    }
  }

  return grpc::Status::OK;
}

Partition::Partition(uint64_t ts, float gps_longitude_begin,
                     float gps_latitude_begin, float gps_longitude_end,
                     float gps_latitude_end)
    : ts_begin_(ts), gps_longitude_begin_(gps_longitude_begin),
      gps_latitude_begin_(gps_latitude_begin),
      gps_longitude_end_(gps_longitude_end),
      gps_latitude_end_(gps_latitude_end) {}

Status Mixer::Init(const MixerConfig &config) {
  RETURN_IF_ERROR(InitHandlers(config));
  RETURN_IF_ERROR(InitService(config));

  return StatusCode::OK;
}

Status Mixer::InitHandlers(const MixerConfig &config) {
  for (const auto &shard : config.ShardConfigs()) {
    auto handler = std::make_shared<ShardHandler>(shard);

    Status status = handler->Init();
    if (status != StatusCode::OK) {
      LOG(WARNING) << "unable to init handler, status=" << status;
    }

    handlers_.push_back(handler);
  }

  // Register handlers per partition.
  for (auto &partition : config.PartitionConfigs()) {
    for (auto &handler : handlers_) {
      if (handler->Name() == partition.shard_) {
        Partition p(partition.ts_, partition.gps_longitude_begin_,
                    partition.gps_latitude_begin_, partition.gps_longitude_end_,
                    partition.gps_latitude_end_);
        partitions_[p] = handler;
        break;
      }
    }
  }

  return StatusCode::OK;
}

Status Mixer::InitService(const MixerConfig &config) {
  grpc::ServerBuilder builder;

  builder.AddListeningPort(config.NetworkAddress(),
                           grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  grpc_ = builder.BuildAndStart();
  LOG(INFO) << "initialized grpc";

  return StatusCode::OK;
}

grpc::Status Mixer::DeleteUser(grpc::ServerContext *context,
                               const proto::DeleteUserRequest *request,
                               proto::DeleteUserResponse *response) {
  for (auto &handler : handlers_) {
    grpc::Status status = handler->DeleteUser(request, response);
    if (!status.ok()) {
      LOG(WARNING) << "unable to delete user in a shard, status="
                   << status.error_message();
      return status;
    }
  }

  LOG(INFO) << "user deleted in all shards";

  return grpc::Status::OK;
}

Status Mixer::Run() {
  utils::WaitForExitSignal();

  grpc_->Shutdown();

  return StatusCode::OK;
}

} // namespace bt
