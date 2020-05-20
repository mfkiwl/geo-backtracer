#pragma once

#include <grpc++/grpc++.h>
#include <map>

#include "common/status.h"
#include "proto/backtrace.grpc.pb.h"
#include "server/mixer_config.h"
#include "server/proto.h"

namespace bt {

// Streams requests to all machines of a specific shard.
class ShardHandler {
public:
  explicit ShardHandler(const ShardConfig &config);
  Status Init();
  const std::string &Name() const;

  grpc::Status DeleteUser(const proto::DeleteUserRequest *request,
                          proto::DeleteUserResponse *response);

private:
  std::vector<std::unique_ptr<proto::Pusher::Stub>> stubs_;
  ShardConfig config_;
};

class PartitionComparator;

// Partitioning key.
class Partition {
public:
  Partition(uint64_t ts, float gps_longitude_begin, float gps_latitude_begin,
            float gps_longitude_end, float gps_latitude_end);

  class Comparator {
  public:
    bool operator()(const Partition &lhs, const Partition &rhs) const {
      return (lhs.ts_begin_ < rhs.ts_begin_) &&
             (lhs.gps_longitude_begin_ < rhs.gps_longitude_begin_) &&
             (lhs.gps_latitude_begin_ < rhs.gps_latitude_begin_) &&
             (lhs.gps_longitude_end_ < rhs.gps_longitude_end_) &&
             (lhs.gps_latitude_end_ < rhs.gps_latitude_end_);
    }
  };

  friend class Comparator;

private:
  uint64_t ts_begin_ = 0;
  float gps_longitude_begin_ = 0.0;
  float gps_latitude_begin_ = 0.0;
  float gps_longitude_end_ = 0.0;
  float gps_latitude_end_ = 0.0;
};

// Main class of the mixer.
class Mixer : public proto::Pusher::Service {
public:
  Status Init(const MixerConfig &config);
  Status Run();

  grpc::Status DeleteUser(grpc::ServerContext *context,
                          const proto::DeleteUserRequest *request,
                          proto::DeleteUserResponse *response) override;

private:
  Status InitHandlers(const MixerConfig &config);
  Status InitService(const MixerConfig &config);

  std::vector<std::shared_ptr<ShardHandler>> handlers_;
  std::map<Partition, std::shared_ptr<ShardHandler>, Partition::Comparator>
      partitions_;
  std::unique_ptr<grpc::Server> grpc_;
};

} // namespace bt
