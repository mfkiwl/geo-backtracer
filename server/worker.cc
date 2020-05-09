#include <glog/logging.h>
#include <thread>

#include "common/signal.h"
#include "common/utils.h"
#include "server/options.h"
#include "server/worker.h"

namespace bt {

namespace {

constexpr char kWorkerAddress[] = "127.0.0.1:6000";

} // anonymous namespace

Status Worker::Init(const Options &options) {
  db_ = std::make_unique<Db>();
  RETURN_IF_ERROR(db_->Init(options));
  LOG(INFO) << "initialized db";

  pusher_ = std::make_unique<Pusher>();
  RETURN_IF_ERROR(pusher_->Init(db_.get()));
  LOG(INFO) << "initialized pusher";

  seeker_ = std::make_unique<Seeker>();
  RETURN_IF_ERROR(seeker_->Init(db_.get()));
  LOG(INFO) << "initialized seeker";

  gc_ = std::make_unique<Gc>();
  RETURN_IF_ERROR(gc_->Init(db_.get(), options));
  LOG(INFO) << "initialized gc";

  grpc::ServerBuilder builder;
  builder.AddListeningPort(kWorkerAddress, grpc::InsecureServerCredentials());
  builder.RegisterService(pusher_.get());
  builder.RegisterService(seeker_.get());
  grpc_ = builder.BuildAndStart();
  LOG(INFO) << "initialized grpc";

  return StatusCode::OK;
}

Status Worker::Run() {
  std::thread grpc_thread([](grpc::Server *s) { s->Wait(); }, grpc_.get());
  std::thread gc_thread(std::thread([](Gc *gc) { gc->Wait(); }, gc_.get()));

  utils::WaitForExitSignal();

  grpc_->Shutdown();
  gc_->Shutdown();

  grpc_thread.join();
  gc_thread.join();

  return StatusCode::OK;
}

} // namespace bt