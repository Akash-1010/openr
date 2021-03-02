/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Watchdog.h"

#include <openr/common/Constants.h>
#include <openr/common/Util.h>

namespace openr {

Watchdog::Watchdog(std::shared_ptr<const Config> config)
    : myNodeName_(config->getNodeName()),
      interval_(*config->getWatchdogConfig().interval_s_ref()),
      threadTimeout_(*config->getWatchdogConfig().thread_timeout_s_ref()),
      maxMemoryMB_(*config->getWatchdogConfig().max_memory_mb_ref()),
      previousStatus_(true) {
  // Schedule periodic timer for checking thread health
  watchdogTimer_ = folly::AsyncTimeout::make(*getEvb(), [this]() noexcept {
    updateCounters();
    monitorMemory();
    // Schedule next timer
    watchdogTimer_->scheduleTimeout(interval_);
  });
  watchdogTimer_->scheduleTimeout(interval_);
}

void
Watchdog::addEvb(OpenrEventBase* evb, const std::string& name) {
  CHECK(evb);
  getEvb()->runInEventBaseThreadAndWait([this, evb, name]() {
    CHECK_EQ(monitorEvbs_.count(evb), 0);
    monitorEvbs_.emplace(evb, name);
  });
}

bool
Watchdog::memoryLimitExceeded() {
  bool result;
  getEvb()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [&result, this]() { result = memExceedTime_.has_value(); });
  return result;
}

void
Watchdog::monitorMemory() {
  auto memInUse_ = systemMetrics_.getRSSMemBytes();
  if (not memInUse_.has_value()) {
    return;
  }
  if (memInUse_.value() / 1e6 > maxMemoryMB_) {
    LOG(WARNING) << "Memory usage critical:" << memInUse_.value() << " bytes,"
                 << " Memory limit:" << maxMemoryMB_ << " MB";
    if (not memExceedTime_.has_value()) {
      memExceedTime_ = std::chrono::steady_clock::now();
      return;
    }
    // check for sustained critical memory usage
    if (std::chrono::steady_clock::now() - memExceedTime_.value() >
        Constants::kMemoryThresholdTime) {
      std::string msg = folly::sformat(
          "Memory limit exceeded the permitted limit."
          " Mem used:{}."
          " Mem Limit:{}",
          memInUse_.value(),
          maxMemoryMB_);
      fireCrash(msg);
    }
    return;
  }
  if (memExceedTime_.has_value()) {
    memExceedTime_ = std::nullopt;
  }
}

void
Watchdog::updateCounters() {
  VLOG(2) << "Checking thread aliveness counters...";

  // Use steady_clock for watchdog as system_clock can change
  auto const& now = std::chrono::steady_clock::now();
  std::vector<std::string> stuckThreads;
  for (auto const& kv : monitorEvbs_) {
    auto const& name = kv.second;
    auto const& lastTs = kv.first->getTimestamp();
    auto timeDiff =
        std::chrono::duration_cast<std::chrono::seconds>(now - lastTs);
    VLOG(4) << "Thread " << name << ", " << (now - lastTs).count()
            << " seconds ever since last thread activity";

    if (timeDiff > threadTimeout_) {
      // fire a crash right now
      LOG(WARNING) << "Watchdog: " << name << " thread detected to be dead";
      stuckThreads.emplace_back(name);
    }
  }

  if (stuckThreads.size() and previousStatus_) {
    LOG(WARNING) << "Watchdog: Waiting for one more round before crashing";
  }

  if (stuckThreads.size() and !previousStatus_) {
    std::string msg = folly::sformat(
        "OpenR DeadThreadDetector: Thread {} on {} is detected dead. "
        "Triggering crash.",
        folly::join(", ", stuckThreads),
        myNodeName_);
    fireCrash(msg);
  }

  if (!stuckThreads.size() and !previousStatus_) {
    LOG(INFO) << "Watchdog: Threads seems to have recovered";
  }

  previousStatus_ = stuckThreads.size() == 0;
}

void
Watchdog::fireCrash(const std::string& msg) {
  SYSLOG(ERROR) << msg;
  // hell ya!
  abort();
}

} // namespace openr
